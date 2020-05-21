/* Copyright JS Foundation and other contributors, http://js.foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecma-array-object.h"
#include "ecma-gc.h"
#include "ecma-globals.h"
#include "ecma-helpers.h"
#include "ecma-lcache.h"
#include "ecma-property-hashmap.h"
#include "jcontext.h"

/** \addtogroup ecma ECMA
 * @{
 *
 * \addtogroup ecmalcache Property lookup cache
 * @{
 */

#if ENABLED (JERRY_LCACHE)

/**
 * Bitshift index for calculating hash.
 */
#if ENABLED (JERRY_CPOINTER_32_BIT)
#define ECMA_LCACHE_HASH_BITSHIFT_INDEX (2 * JMEM_ALIGNMENT_LOG)
#else /* !ENABLED (JERRY_CPOINTER_32_BIT) */
#define ECMA_LCACHE_HASH_BITSHIFT_INDEX 0
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */

/**
 * Mask for hash bits
 */
#define ECMA_LCACHE_HASH_MASK ((ECMA_LCACHE_HASH_ROWS_COUNT - 1) << ECMA_LCACHE_HASH_BITSHIFT_INDEX)

/**
 * Bitshift index for creating property identifier
 */
#define ECMA_LCACHE_HASH_ENTRY_ID_SHIFT (8 * sizeof (jmem_cpointer_t))

/**
 * Create property identifier
 */
#define ECMA_LCACHE_CREATE_ID(object_cp, name_cp) \
  (((ecma_lcache_hash_entry_id_t) (object_cp) << ECMA_LCACHE_HASH_ENTRY_ID_SHIFT) | (name_cp))

/**
 * Compute the row index of object / property name pair
 *
 * @return row index
 */
static inline size_t JERRY_ATTR_ALWAYS_INLINE
ecma_lcache_row_index (jmem_cpointer_t object_cp, /**< compressed pointer to object */
                       jmem_cpointer_t name_cp) /**< compressed pointer to property name */
{
  /* Randomize the property name with the object pointer using a xor operation,
   * so properties of different objects with the same name can be cached effectively. */
  return (size_t) (((name_cp ^ object_cp) & ECMA_LCACHE_HASH_MASK) >> ECMA_LCACHE_HASH_BITSHIFT_INDEX);
} /* ecma_lcache_row_index */

/**
 * Insert an entry into LCache
 */
void
ecma_lcache_insert (const ecma_object_t *object_p, /**< object */
                    const jmem_cpointer_t name_cp, /**< property name */
                    const ecma_property_index_t prop_index) /** property index */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p)
                || !ecma_op_object_is_fast_array ((ecma_object_t *) object_p));

  jmem_cpointer_t object_cp;

  ECMA_SET_NON_NULL_POINTER (object_cp, object_p);

  size_t row_index = ecma_lcache_row_index (object_cp, name_cp);
  ecma_lcache_hash_entry_t *entry_p = JERRY_CONTEXT (lcache) [row_index];
  ecma_lcache_hash_entry_t *entry_end_p = entry_p + ECMA_LCACHE_HASH_ROW_LENGTH;

  do
  {
    if (entry_p->id == 0)
    {
      goto insert;
    }

    entry_p++;
  }
  while (entry_p < entry_end_p);

  --entry_p;
  entry_p->id = 0;

  /* Shift other entries towards the end. */
  for (uint32_t i = 0; i < ECMA_LCACHE_HASH_ROW_LENGTH - 1; i++)
  {
    entry_p->id = entry_p[-1].id;
    entry_p->prop_idx = entry_p[-1].prop_idx;
    entry_p--;
  }

insert:
  entry_p->prop_idx = prop_index;
  entry_p->id = ECMA_LCACHE_CREATE_ID (object_cp, name_cp);
} /* ecma_lcache_insert */

/**
 * Lookup property in the LCache
 *
 * @return a pointer to an ecma_property_t if the lookup is successful
 *         NULL otherwise
 */
inline ecma_property_t * JERRY_ATTR_ALWAYS_INLINE
ecma_lcache_lookup (const ecma_object_t *object_p, /**< object */
                    const ecma_string_t *prop_name_p) /**< property's name */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (prop_name_p != NULL);

  if (JERRY_UNLIKELY (object_p->u1.property_header_cp == JMEM_CP_NULL))
  {
    return NULL;
  }

  jmem_cpointer_t object_cp;
  ECMA_SET_NON_NULL_POINTER (object_cp, object_p);

  uint8_t prop_name_type = ECMA_DIRECT_STRING_PTR;
  jmem_cpointer_t prop_name_cp;

  if (JERRY_UNLIKELY (ECMA_IS_DIRECT_STRING (prop_name_p)))
  {
    prop_name_type = (uint8_t) ECMA_GET_DIRECT_STRING_TYPE (prop_name_p);
    prop_name_cp = (jmem_cpointer_t) ECMA_GET_DIRECT_STRING_VALUE (prop_name_p);
  }
  else
  {
    ECMA_SET_NON_NULL_POINTER (prop_name_cp, prop_name_p);
  }

  size_t row_index = ecma_lcache_row_index (object_cp, prop_name_cp);

  ecma_lcache_hash_entry_t *entry_p = JERRY_CONTEXT (lcache) [row_index];
  ecma_lcache_hash_entry_t *entry_end_p = entry_p + ECMA_LCACHE_HASH_ROW_LENGTH;
  ecma_lcache_hash_entry_id_t id = ECMA_LCACHE_CREATE_ID (object_cp, prop_name_cp);

  do
  {
    if (entry_p->id == id)
    {
      JERRY_ASSERT (entry_p->prop_idx != ECMA_PROPERTY_INDEX_INVALID);

      ecma_property_header_t *property_header_p = ECMA_GET_NON_NULL_POINTER (ecma_property_header_t,
                                                                             object_p->u1.property_header_cp);
      ecma_property_t *property_p = (ecma_property_t *) property_header_p + entry_p->prop_idx;

      JERRY_ASSERT (property_p != NULL);

      if (JERRY_LIKELY (ECMA_PROPERTY_GET_NAME_TYPE (property_p) == prop_name_type))
      {
        return property_p;
      }
    }

    entry_p++;
  }
  while (entry_p < entry_end_p);

  return NULL;
} /* ecma_lcache_lookup */

#if 0
/**
 * Invalidate LCache entries associated with given object and property name / property
 */
void
ecma_lcache_invalidate (const ecma_object_t *object_p, /**< object */
                        ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (property_p != NULL);
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_INTERNAL);

  jmem_cpointer_t object_cp;
  ECMA_SET_NON_NULL_POINTER (object_cp, object_p);

  size_t row_index = ecma_lcache_row_index (object_cp, property_p->name_cp);
  ecma_lcache_hash_entry_t *entry_p = JERRY_CONTEXT (lcache) [row_index];
  ecma_lcache_hash_entry_t *entry_end_p = entry_p + ECMA_LCACHE_HASH_ROW_LENGTH;
  ecma_lcache_hash_entry_id_t id = ECMA_LCACHE_CREATE_ID (object_cp, property_p->name_cp);

  do
  {
    if (entry_p->id == id)
    {
      entry_p->id = 0;

      return;
    }

    entry_p++;
  }
  while (entry_p < entry_end_p);
} /* ecma_lcache_invalidate */
#endif

#endif /* ENABLED (JERRY_LCACHE) */

/**
 * @}
 * @}
 */
