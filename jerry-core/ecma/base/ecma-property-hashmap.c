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

/**
 * Compute the total size of the property hashmap.
 */
#define ECMA_HASHMAP_GET_BUCKET_SIZE(max_property_count) \
  (max_property_count * sizeof (ecma_hashmap_bucket_header_t))

/**
 * Compute the total size of the property hashmap.
 */
#define ECMA_HASHMAP_GET_TOTAL_SIZE(max_property_count) \
  (sizeof (ecma_hashmap_header_t) + ECMA_HASHMAP_GET_BUCKET_SIZE (max_property_count))

#if ENABLED (JERRY_PROPRETY_HASHMAP)
/**
 * Create a new property hashmap for the object.
 * The object must not has a property hashmap.
 */
void
ecma_property_hashmap_create (ecma_property_header_t *property_header_p) /**< object */
{
  if (JERRY_CONTEXT (ecma_prop_hashmap_alloc_state) != ECMA_PROP_HASHMAP_ALLOC_ON)
  {
    return;
  }

  JERRY_ASSERT (property_header_p != NULL);
  /* The object must not has a property hashmap. */
  JERRY_ASSERT (property_header_p->cache[0] != 0);

  /* TODO: determine the named properties well. The count slot can contain deleted properties. */
  if (property_header_p->count < (ECMA_PROPERTY_HASMAP_MINIMUM_SIZE / 2))
  {
    return;
  }

  size_t total_size = ECMA_HASHMAP_GET_TOTAL_SIZE (property_header_p->count / 2);
  ecma_hashmap_header_t *hashmap_p = (ecma_hashmap_header_t *) jmem_heap_alloc_block_null_on_error (total_size);

  if (hashmap_p == NULL)
  {
    return;
  }

  memset (hashmap_p, 0, total_size);

  hashmap_p->property_count = property_header_p->count;
  hashmap_p->bucket_count = property_header_p->count / 2;
  // Mark that property list hash hashmap.
  property_header_p->cache[0] = 0;

  ECMA_SET_NON_NULL_POINTER (property_header_p->cache[1], hashmap_p);

  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
  ecma_property_index_t property_count = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

  ecma_hashmap_bucket_header_t *buckets = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);

  for (ecma_property_index_t index = 0; index < property_count; index++)
  {
    ecma_property_t *curr_property_p = property_start_p + index;

    if (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p))
    {
      uint32_t hash = ecma_string_get_property_name_hash (curr_property_p);
      uint32_t bucket_count = hashmap_p->bucket_count;

      ecma_hashmap_bucket_header_t *bucket = &buckets[hash % bucket_count];
      bucket->count++;

      ecma_hashmap_entry_t *entry_p = jmem_heap_alloc_block (sizeof (ecma_hashmap_entry_t));

      entry_p->index = (ecma_property_index_t) (index + 1u);
      entry_p->next_cp = bucket->next_cp;

      ECMA_SET_NON_NULL_POINTER (bucket->next_cp, entry_p);
    }
  }
}

/**
 * Free the hashmap of the object.
 * The object must have a property hashmap.
 */
void
ecma_property_hashmap_free (ecma_property_header_t *property_header_p) /**< object */
{
  /* Hashmap must be exists. */
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                property_header_p->cache[1]);

  ecma_hashmap_bucket_header_t *buckets = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);

  for (uint32_t i = 0; i < hashmap_p->bucket_count; i++)
  {
    if (buckets[i].next_cp != JMEM_CP_NULL)
    {
      jmem_cpointer_t next_cp = buckets[i].next_cp;

      while (next_cp != JMEM_CP_NULL)
      {
        ecma_hashmap_entry_t *entry_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_entry_t, next_cp);

        next_cp = entry_p->next_cp;

        jmem_heap_free_block (entry_p, sizeof (ecma_hashmap_entry_t));
      }
    }
  }

  /* Restore the local cache. */
  for (uint32_t i = 0; i < ECMA_PROPERTY_CACHE_SIZE; i++)
  {
    property_header_p->cache[i] = 1;
  }

  uint32_t bucket_count = hashmap_p->bucket_count;
  size_t total_size = sizeof (ecma_hashmap_header_t) + bucket_count * sizeof (ecma_hashmap_bucket_header_t);

  jmem_heap_free_block (hashmap_p, total_size);
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
  JERRY_ASSERT (name_p != NULL);

  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                property_header_p->cache[1]);

//  Let hashmap to be bigger if more elements are added.
//  if (property_header_p->count > 2 * hashmap_p->property_count)
//  {
//    ecma_property_hashmap_free (ecma_hashmap_header_t);
//    ecma_property_hashmap_create (ecma_hashmap_header_t);
//    return;
//  }

  ecma_hashmap_bucket_header_t *buckets = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);

  uint32_t hash = ecma_string_hash (name_p);
  uint32_t bucket_count = hashmap_p->bucket_count;

  ecma_hashmap_bucket_header_t *bucket = &buckets[hash % bucket_count];
  bucket->count++;

  ecma_hashmap_entry_t *entry_p = jmem_heap_alloc_block (sizeof (ecma_hashmap_entry_t));

  entry_p->index = index;
  entry_p->next_cp = bucket->next_cp;

  ECMA_SET_NON_NULL_POINTER (bucket->next_cp, entry_p);
}

ecma_property_hashmap_delete_status
ecma_property_hashmap_delete (ecma_property_header_t *property_header_p, /**< object */
                              ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                (jmem_cpointer_t) property_header_p->cache[1]);

//  Let hashmap to be smaller if more elements are deleted.
//  if (hashmap_p->unused_count < hashmap_p->property_count / 2)
//  {
//    return ECMA_PROPERTY_HASHMAP_DELETE_RECREATE_HASHMAP;
//  }

  ecma_hashmap_bucket_header_t *buckets = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);

  uint32_t hash = ecma_string_get_property_name_hash (property_p);
  uint32_t bucket_count = hashmap_p->bucket_count;

  ecma_hashmap_bucket_header_t *bucket = &buckets[hash % bucket_count];
  ecma_hashmap_entry_t *prev_entry_p = (ecma_hashmap_entry_t *) bucket;
  jmem_cpointer_t next_cp = bucket->next_cp;

  while (next_cp != JMEM_CP_NULL)
  {
    ecma_hashmap_entry_t *entry_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_entry_t, next_cp);

    ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + entry_p->index;

    if (curr_property_p == property_p)
    {
      prev_entry_p->next_cp = entry_p->next_cp;
      jmem_heap_free_block (entry_p, sizeof (ecma_hashmap_entry_t));

      bucket->count--;
      break;
    }

    prev_entry_p = entry_p;
    next_cp = entry_p->next_cp;
  }

  return ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
}

ecma_property_t *
ecma_property_hashmap_find (ecma_property_header_t *property_header_p, /**< hashmap */
                            ecma_string_t *name_p, /**< property name */
                            jmem_cpointer_t *property_real_name_cp, /**< [out] property real name */
                            ecma_property_index_t *property_index) /**< [out] index of property */
{
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (name_p != NULL);
  JERRY_ASSERT (property_index != NULL);
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                (jmem_cpointer_t) property_header_p->cache[1]);

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

  uint32_t hash = ecma_string_hash (name_p);
  uint32_t bucket_count = hashmap_p->bucket_count;

  ecma_hashmap_bucket_header_t *buckets = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);
  ecma_hashmap_bucket_header_t *bucket = &buckets[hash % bucket_count];

  jmem_cpointer_t next_cp = bucket->next_cp;

  if (ECMA_IS_DIRECT_STRING (name_p))
  {
    uint8_t prop_name_type = (uint8_t) ECMA_GET_DIRECT_STRING_TYPE (name_p);
    jmem_cpointer_t property_name_cp = (jmem_cpointer_t) ECMA_GET_DIRECT_STRING_VALUE (name_p);

    JERRY_ASSERT (prop_name_type > 0);

    while (next_cp != JMEM_CP_NULL)
    {
      ecma_hashmap_entry_t *entry_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_entry_t, next_cp);
      ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + entry_p->index;

      JERRY_ASSERT (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p));

      if (curr_property_p->name_cp == property_name_cp
          && ECMA_PROPERTY_GET_NAME_TYPE (curr_property_p) == prop_name_type)
      {
  #ifndef JERRY_NDEBUG
        JERRY_ASSERT (property_found);
  #endif /* !JERRY_NDEBUG */

        *property_real_name_cp = property_name_cp;
        *property_index = entry_p->index;

        return curr_property_p;
      }

      next_cp = entry_p->next_cp;
    }

    #ifndef JERRY_NDEBUG
      JERRY_ASSERT (!property_found);
    #endif /* !JERRY_NDEBUG */

    return NULL;
  }

  while (next_cp != JMEM_CP_NULL)
  {
    ecma_hashmap_entry_t *entry_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_entry_t, next_cp);
    ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + entry_p->index;

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
        *property_index = entry_p->index;

        return curr_property_p;
      }
    }

    next_cp = entry_p->next_cp;
  }

#ifndef JERRY_NDEBUG
  JERRY_ASSERT (!property_found);
#endif /* !JERRY_NDEBUG */

  return NULL;
}

#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

/**
 * @}
 * @}
 */
