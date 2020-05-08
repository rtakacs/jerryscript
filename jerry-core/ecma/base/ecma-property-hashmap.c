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

#include "ecma-globals.h"
#include "ecma-helpers.h"
#include "ecma-property-hashmap.h"
#include "jrt-libc-includes.h"
#include "jcontext.h"

/** \addtogroup ecma ECMA
 * @{
 *
 * \addtogroup ecmapropertyhashmap Property hashmap
 * @{
 */

#if ENABLED (JERRY_PROPRETY_HASHMAP)

/**
 * Clean enrty marker.
 */
#define ECMA_PROPERTY_HASHMAP_CLEAN_ENTRY ECMA_PROPERTY_INDEX_INVALID

/**
 * Deleted entry marker.
 */
#define ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY (ECMA_PROPERTY_INDEX_INVALID - 1u)

/**
 * Hashmap memory fill pattern.
 *
 * Note:
 *   This pattern is used in case of memset to initialize the entries as clean.
 */
#define ECMA_PROPERTY_HASHMAP_FILL_PATTERN 0xff

/**
 * Compute the total size of the property hashmap.
 */
#define ECMA_PROPERTY_HASHMAP_GET_ENTRY_SIZE(max_property_count) \
  (max_property_count * sizeof (ecma_property_index_t))

/**
 * Compute the total size of the property hashmap.
 */
#define ECMA_PROPERTY_HASHMAP_GET_TOTAL_SIZE(max_property_count) \
  (sizeof (ecma_property_hashmap_t) + ECMA_PROPERTY_HASHMAP_GET_ENTRY_SIZE (max_property_count))

/**
 * Number of items in the stepping table.
 */
#define ECMA_PROPERTY_HASHMAP_NUMBER_OF_STEPS 8

/**
 * Stepping values for searching items in the hashmap.
 */
static const uint8_t ecma_property_hashmap_steps[ECMA_PROPERTY_HASHMAP_NUMBER_OF_STEPS] JERRY_ATTR_CONST_DATA =
{
  3, 5, 7, 11, 13, 17, 19, 23
};

/**
 * Create a new property hashmap for the object.
 * The object must not have a property hashmap.
 */
void
ecma_property_hashmap_create (ecma_property_header_t *property_header_p) /**< object */
{
  if (JERRY_CONTEXT (ecma_prop_hashmap_alloc_state) != ECMA_PROP_HASHMAP_ALLOC_ON)
  {
    return;
  }

  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] != 0);

  ecma_property_index_t property_count = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

  if (property_count < (ECMA_PROPERTY_HASMAP_MINIMUM_SIZE / 2))
  {
    return;
  }

  /* The max_property_count must be power of 2. */
  uint32_t max_property_count = ECMA_PROPERTY_HASMAP_MINIMUM_SIZE;

  /* At least 1/3 items must be NULL. */
  while (max_property_count < (property_count + (property_count >> 1)))
  {
    max_property_count <<= 1;
  }

  size_t total_size = ECMA_PROPERTY_HASHMAP_GET_TOTAL_SIZE (max_property_count);
  size_t entry_size = ECMA_PROPERTY_HASHMAP_GET_ENTRY_SIZE (max_property_count);

  ecma_property_hashmap_t *hashmap_p = (ecma_property_hashmap_t *) jmem_heap_alloc_block_null_on_error (total_size);

  if (hashmap_p == NULL)
  {
    return;
  }

  property_header_p->cache[0] = 0;
  ECMA_SET_NON_NULL_POINTER (property_header_p->cache[1], hashmap_p);

  hashmap_p->max_property_count = max_property_count;
  hashmap_p->null_count = max_property_count - property_count;
  hashmap_p->unused_count = max_property_count - property_count;

  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
  ecma_property_index_t *pair_list_p = (ecma_property_index_t *) (hashmap_p + 1);
  memset (pair_list_p, ECMA_PROPERTY_HASHMAP_FILL_PATTERN, entry_size);

  uint32_t mask = max_property_count - 1;

  for (ecma_property_index_t i = 0; i < property_count; i++)
  {
    JERRY_ASSERT (i < ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY);

    ecma_property_t *curr_property_p = property_start_p + i;

    JERRY_ASSERT (ECMA_PROPERTY_IS_PROPERTY (curr_property_p));

    if (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p))
    {
      uint32_t entry_index = ecma_string_get_property_name_hash (curr_property_p);
      uint32_t step = ecma_property_hashmap_steps[entry_index & (ECMA_PROPERTY_HASHMAP_NUMBER_OF_STEPS - 1)];

      entry_index &= mask;
#ifndef JERRY_NDEBUG
      /* Because max_property_count (power of 2) and step (a prime
       * number) are relative primes, all entries of the hasmap are
       * visited exactly once before the start entry index is reached
       * again. Furthermore because at least one NULL is present in
       * the hashmap, the while loop must be terminated before the
       * the starting index is reached again. */
      uint32_t start_entry_index = entry_index;
#endif /* !JERRY_NDEBUG */

      while (pair_list_p[entry_index] < ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY)
      {
        entry_index = (entry_index + step) & mask;

#ifndef JERRY_NDEBUG
        JERRY_ASSERT (entry_index != start_entry_index);
#endif /* !JERRY_NDEBUG */
      }

      pair_list_p[entry_index] = (ecma_property_index_t) (i + 1u);
    }
  }
} /* ecma_property_hashmap_create */

/**
 * Free the hashmap of the object.
 * The object must have a property hashmap.
 */
void
ecma_property_hashmap_free (ecma_property_header_t *property_header_p) /**< object */
{
  /* Property hash must be exists and must be the first property. */
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_property_hashmap_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_property_hashmap_t,
                                                                  property_header_p->cache[1]);

  jmem_heap_free_block (hashmap_p,
                        ECMA_PROPERTY_HASHMAP_GET_TOTAL_SIZE (hashmap_p->max_property_count));

  for (uint32_t i = 0; i < ECMA_PROPERTY_CACHE_SIZE; i++)
  {
    property_header_p->cache[i] = 1;
  }
} /* ecma_property_hashmap_free */

/**
 * Insert named property into the hashmap.
 */
void
ecma_property_hashmap_insert (ecma_property_header_t *property_header_p, /**< object */
                              ecma_string_t *name_p, /**< name of the property */
                              ecma_property_index_t index) /**< index of the property in the property list */
{
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] == 0);
  JERRY_ASSERT (name_p != NULL);

  ecma_property_hashmap_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_property_hashmap_t,
                                                                  property_header_p->cache[1]);

  /* The NULLs are reduced below 1/8 of the hashmap. */
  if (hashmap_p->null_count < (hashmap_p->max_property_count >> 3))
  {
    ecma_property_hashmap_free (property_header_p);
    ecma_property_hashmap_create (property_header_p);
    return;
  }

  uint32_t entry_index = ecma_string_hash (name_p);
  uint32_t step = ecma_property_hashmap_steps[entry_index & (ECMA_PROPERTY_HASHMAP_NUMBER_OF_STEPS - 1)];
  uint32_t mask = hashmap_p->max_property_count - 1;
  entry_index &= mask;

#ifndef JERRY_NDEBUG
  /* See the comment for this variable in ecma_property_hashmap_create. */
  uint32_t start_entry_index = entry_index;
#endif /* !JERRY_NDEBUG */

  ecma_property_index_t *pair_list_p = (ecma_property_index_t *) (hashmap_p + 1);

  while (pair_list_p[entry_index] < ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY)
  {
    entry_index = (entry_index + step) & mask;

#ifndef JERRY_NDEBUG
    JERRY_ASSERT (entry_index != start_entry_index);
#endif /* !JERRY_NDEBUG */
  }

  if (pair_list_p[entry_index] == ECMA_PROPERTY_HASHMAP_CLEAN_ENTRY)
  {
    /* Deleted entries also has ECMA_NULL_POINTER
     * value, but they are not NULL values. */
    hashmap_p->null_count--;
    JERRY_ASSERT (hashmap_p->null_count > 0);
  }

  hashmap_p->unused_count--;
  JERRY_ASSERT (hashmap_p->unused_count > 0);

  pair_list_p[entry_index] = index;
} /* ecma_property_hashmap_insert */

/**
 * Delete named property from the hashmap.
 *
 * @return ECMA_PROPERTY_HASHMAP_DELETE_RECREATE_HASHMAP if hashmap should be recreated
 *         ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP otherwise
 */
ecma_property_hashmap_delete_status
ecma_property_hashmap_delete (ecma_property_header_t *property_header_p, /**< object */
                              ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] == 0);
  JERRY_ASSERT (property_p != NULL);

  ecma_property_hashmap_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_property_hashmap_t,
                                                                  property_header_p->cache[1]);

  hashmap_p->unused_count++;

  /* The NULLs are above 3/4 of the hashmap. */
  if (hashmap_p->unused_count > ((hashmap_p->max_property_count * 3) >> 2))
  {
    return ECMA_PROPERTY_HASHMAP_DELETE_RECREATE_HASHMAP;
  }

  uint32_t entry_index = ecma_string_get_property_name_hash (property_p);
  uint32_t step = ecma_property_hashmap_steps[entry_index & (ECMA_PROPERTY_HASHMAP_NUMBER_OF_STEPS - 1)];
  uint32_t mask = hashmap_p->max_property_count - 1;
  ecma_property_index_t *pair_list_p = (ecma_property_index_t *) (hashmap_p + 1);

  entry_index &= mask;

#ifndef JERRY_NDEBUG
  /* See the comment for this variable in ecma_property_hashmap_create. */
  uint32_t start_entry_index = entry_index;
#endif /* !JERRY_NDEBUG */

  while (true)
  {
    if (pair_list_p[entry_index] < ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY)
    {
      ecma_property_index_t property_index = pair_list_p[entry_index];

      ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + property_index;

      if (curr_property_p == property_p)
      {
        JERRY_ASSERT (curr_property_p->name_cp == property_p->name_cp);

        pair_list_p[entry_index] = ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY;
        return ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
      }
    }

    entry_index = (entry_index + step) & mask;

#ifndef JERRY_NDEBUG
    JERRY_ASSERT (entry_index != start_entry_index);
#endif /* !JERRY_NDEBUG */
  }
} /* ecma_property_hashmap_delete */

/**
 * Find a named property.
 *
 * @return pointer to the property if found or NULL otherwise
 */
ecma_property_t *
ecma_property_hashmap_find (ecma_property_header_t *property_header_p, /**< hashmap */
                            ecma_string_t *name_p, /**< property name */
                            jmem_cpointer_t *property_real_name_cp, /**< [out] property real name */
                            ecma_property_index_t *property_index) /**< [out] index of property */
{
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] == 0);
  JERRY_ASSERT (name_p != NULL);
  JERRY_ASSERT (property_index != NULL);

  ecma_property_hashmap_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_property_hashmap_t,
                                                                  property_header_p->cache[1]);

#ifndef JERRY_NDEBUG
  /* A sanity check in debug mode: a named property must be present
   * in both the property hashmap and in the property chain, or missing
   * from both data collection. The following code checks the property
   * chain, and sets the property_found variable. */
  bool property_found = false;

  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
  ecma_property_index_t property_count = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

  for (ecma_property_index_t i = 0; i < property_count; i++)
  {
    ecma_property_t *curr_property_p = property_start_p + i;
    JERRY_ASSERT (ECMA_PROPERTY_IS_PROPERTY (curr_property_p));

    if (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p))
    {
      if (ecma_string_compare_to_property_name (curr_property_p, name_p))
      {
        /* Property is found */
        property_found = true;
        break;
      }
    }
  }
#endif /* !JERRY_NDEBUG */

  uint32_t entry_index = ecma_string_hash (name_p);
  uint32_t step = ecma_property_hashmap_steps[entry_index & (ECMA_PROPERTY_HASHMAP_NUMBER_OF_STEPS - 1)];
  uint32_t mask = hashmap_p->max_property_count - 1;
  ecma_property_index_t *pair_list_p = (ecma_property_index_t *) (hashmap_p + 1);
  entry_index &= mask;

#ifndef JERRY_NDEBUG
  /* See the comment for this variable in ecma_property_hashmap_create. */
  uint32_t start_entry_index = entry_index;
#endif /* !JERRY_NDEBUG */

  if (ECMA_IS_DIRECT_STRING (name_p))
  {
    uint8_t prop_name_type = (uint8_t) ECMA_GET_DIRECT_STRING_TYPE (name_p);
    jmem_cpointer_t property_name_cp = (jmem_cpointer_t) ECMA_GET_DIRECT_STRING_VALUE (name_p);

    JERRY_ASSERT (prop_name_type > 0);

    while (true)
    {
      ecma_property_index_t index = pair_list_p[entry_index];

      if (index != ECMA_PROPERTY_HASHMAP_CLEAN_ENTRY)
      {
        if (index != ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY)
        {
          ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + index;

          JERRY_ASSERT (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p));

          if (curr_property_p->name_cp == property_name_cp
              && ECMA_PROPERTY_GET_NAME_TYPE (curr_property_p) == prop_name_type)
          {
  #ifndef JERRY_NDEBUG
            JERRY_ASSERT (property_found);
  #endif /* !JERRY_NDEBUG */

            *property_real_name_cp = property_name_cp;
            *property_index = index;

            return curr_property_p;
          }
        }
      }
      else
      {
#ifndef JERRY_NDEBUG
        JERRY_ASSERT (!property_found);
#endif /* !JERRY_NDEBUG */

        return NULL;
      }

      entry_index = (entry_index + step) & mask;

#ifndef JERRY_NDEBUG
      JERRY_ASSERT (entry_index != start_entry_index);
#endif /* !JERRY_NDEBUG */
    }
  }

  while (true)
  {
    ecma_property_index_t index = pair_list_p[entry_index];

    if (index != ECMA_PROPERTY_HASHMAP_CLEAN_ENTRY)
    {
      if (index != ECMA_PROPERTY_HASHMAP_DIRTY_ENTRY)
      {
        ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + index;

        JERRY_ASSERT (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p));

        if (ECMA_PROPERTY_GET_NAME_TYPE (curr_property_p) == ECMA_DIRECT_STRING_PTR)
        {
          ecma_string_t *prop_name_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t, curr_property_p->name_cp);

          if (ecma_compare_ecma_non_direct_strings (prop_name_p, name_p))
          {
#ifndef JERRY_NDEBUG
            JERRY_ASSERT (property_found);
#endif /* !JERRY_NDEBUG */

            *property_real_name_cp = curr_property_p->name_cp;
            *property_index = index;

            return curr_property_p;
          }
        }
      }
    }
    else
    {
#ifndef JERRY_NDEBUG
      JERRY_ASSERT (!property_found);
#endif /* !JERRY_NDEBUG */

      return NULL;
    }

    entry_index = (entry_index + step) & mask;

#ifndef JERRY_NDEBUG
    JERRY_ASSERT (entry_index != start_entry_index);
#endif /* !JERRY_NDEBUG */
  }
} /* ecma_property_hashmap_find */
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

/**
 * @}
 * @}
 */
