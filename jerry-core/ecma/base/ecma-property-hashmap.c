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
#include "ecma-lcache.h"
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
 * Compute the total size of the property hashmap.
 */
#define ECMA_HASHMAP_GET_TOTAL_SIZE(bucket_count) \
  (sizeof (ecma_hashmap_header_t) + bucket_count * sizeof (ecma_hashmap_bucket_header_t))

/**
 * Size of an entry.
 */
#define ECMA_HASHMAP_CHUNK_SIZE 8

/**
 * Number of indexes in a chunk.
 */
#define ECMA_HASHMAP_GROWTH_FACTOR (ECMA_HASHMAP_CHUNK_SIZE / sizeof (ecma_property_index_t))

/**
 * Create a new property hashmap for the object.
 * The object must not has a property hashmap.
 */
void
ecma_property_hashmap_create (ecma_property_header_t *property_header_p) /**< object */
{
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] != 0);

  if (JERRY_CONTEXT (ecma_prop_hashmap_alloc_state) != ECMA_PROP_HASHMAP_ALLOC_ON)
  {
    return;
  }

  if (property_header_p->count < (ECMA_PROPERTY_HASMAP_MINIMUM_SIZE >> 1))
  {
    return;
  }

  ecma_property_index_t bucket_count = property_header_p->count >> 2;

  size_t total_size = ECMA_HASHMAP_GET_TOTAL_SIZE (bucket_count);
  ecma_hashmap_header_t *hashmap_p = jmem_heap_alloc_block_null_on_error (total_size);

  if (hashmap_p == NULL)
  {
    return;
  }

  memset (hashmap_p, 0, total_size);
  hashmap_p->bucket_count = bucket_count;

  /* Mark hashmap on the property list. */
  property_header_p->cache[0] = 0;
  ECMA_SET_NON_NULL_POINTER (property_header_p->cache[1], hashmap_p);
} /* ecma_property_hashmap_create */

/**
 * Free the hashmap of the object.
 * The object must have a property hashmap.
 */
void
ecma_property_hashmap_free (ecma_property_header_t *property_header_p) /**< object */
{
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                property_header_p->cache[1]);

  ecma_hashmap_bucket_header_t *bucket_start_p = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);

//  printf("num_of_buckets: %d - num_of_properties: %d\n", (int) hashmap_p->bucket_count, (int) property_header_p->count);

  for (uint32_t i = 0; i < hashmap_p->bucket_count; i++)
  {
    ecma_hashmap_bucket_header_t *bucket_p = bucket_start_p + i;

    if (bucket_p->index_list_cp != JMEM_CP_NULL)
    {
      ecma_property_index_t *index_list_p = ECMA_GET_NON_NULL_POINTER (ecma_property_index_t,
                                                                       bucket_p->index_list_cp);

//      printf("capacity: %d - used: %d\n", (int) bucket_p->capacity, (int) bucket_p->count);

      jmem_heap_free_block (index_list_p, bucket_p->capacity * sizeof (ecma_property_index_t));
    }
  }

  /* Restore the local cache. */
  for (uint32_t i = 0; i < ECMA_PROPERTY_CACHE_SIZE; i++)
  {
    property_header_p->cache[i] = 1;
  }

  jmem_heap_free_block (hashmap_p, ECMA_HASHMAP_GET_TOTAL_SIZE (hashmap_p->bucket_count));
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

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                property_header_p->cache[1]);

  uint32_t hash = ecma_string_hash (name_p);
  uint32_t bucket_index = hash % hashmap_p->bucket_count;

  ecma_hashmap_bucket_header_t *bucket_start_p = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);
  ecma_hashmap_bucket_header_t *bucket_p = bucket_start_p + bucket_index;

  ecma_property_index_t *index_list_p = NULL;

  if (JERRY_UNLIKELY (bucket_p->index_list_cp == JMEM_CP_NULL))
  {
    index_list_p = jmem_heap_alloc_block (ECMA_HASHMAP_CHUNK_SIZE);
    ECMA_SET_NON_NULL_POINTER (bucket_p->index_list_cp, index_list_p);

    bucket_p->capacity = ECMA_HASHMAP_GROWTH_FACTOR;
  }
  else
  {
    index_list_p = ECMA_GET_NON_NULL_POINTER (ecma_property_index_t, bucket_p->index_list_cp);

    if (bucket_p->count == bucket_p->capacity)
    {
      size_t old_size = bucket_p->capacity * sizeof (ecma_property_index_t);
      size_t new_size = old_size + ECMA_HASHMAP_CHUNK_SIZE;

      index_list_p = jmem_heap_realloc_block (index_list_p, old_size, new_size);
      ECMA_SET_NON_NULL_POINTER (bucket_p->index_list_cp, index_list_p);

      bucket_p->capacity = (ecma_property_index_t)(bucket_p->capacity + ECMA_HASHMAP_GROWTH_FACTOR);
    }
  }

  // Append property to the end of the list.
  index_list_p[bucket_p->count++] = index;
}

ecma_property_hashmap_delete_status
ecma_property_hashmap_delete (ecma_property_header_t *property_header_p, /**< object */
                              ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                (jmem_cpointer_t) property_header_p->cache[1]);

  uint32_t hash = ecma_string_get_property_name_hash (property_p);
  uint32_t bucket_index = hash % hashmap_p->bucket_count;

  ecma_hashmap_bucket_header_t *bucket_start_p = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);
  ecma_hashmap_bucket_header_t *bucket_p = bucket_start_p + bucket_index;

  if (bucket_p->index_list_cp == JMEM_CP_NULL)
  {
    return ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
  }

  ecma_property_index_t *index_list_p = ECMA_GET_NON_NULL_POINTER (ecma_property_index_t, bucket_p->index_list_cp);

  for (uint8_t i = 0; i < bucket_p->count; i++)
  {
    ecma_property_index_t property_index = index_list_p[i];

    if (property_index != 0)
    {
      ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + property_index;

      if (curr_property_p == property_p)
      {
        index_list_p[i] = 0;

        return ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
      }
    }
  }

  return ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
}

ecma_property_t *
ecma_property_hashmap_find (ecma_object_t *obj_p, /**< object pointer */
                            ecma_property_header_t *property_header_p, /**< property header pointer */
                            ecma_string_t *name_p) /**< property name */
{
  JERRY_ASSERT (obj_p != NULL);
  JERRY_ASSERT (name_p != NULL);
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                (jmem_cpointer_t) property_header_p->cache[1]);

  uint32_t hash = ecma_string_hash (name_p);
  uint32_t bucket_index = hash % hashmap_p->bucket_count;

  ecma_hashmap_bucket_header_t *bucket_start_p = (ecma_hashmap_bucket_header_t *)(hashmap_p + 1);
  ecma_hashmap_bucket_header_t *bucket_p = bucket_start_p + bucket_index;

  if (bucket_p->index_list_cp == JMEM_CP_NULL)
  {
    return NULL;
  }

  ecma_property_index_t *index_list_p = ECMA_GET_NON_NULL_POINTER (ecma_property_index_t, bucket_p->index_list_cp);

  if (ECMA_IS_DIRECT_STRING (name_p))
  {
    uint8_t prop_name_type = (uint8_t) ECMA_GET_DIRECT_STRING_TYPE (name_p);
    jmem_cpointer_t property_name_cp = (jmem_cpointer_t) ECMA_GET_DIRECT_STRING_VALUE (name_p);

    JERRY_ASSERT (prop_name_type > 0);

    for (uint8_t i = 0; i < bucket_p->count; i++)
    {
      ecma_property_index_t property_index = index_list_p[i];

      if (property_index != 0)
      {
        ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + property_index;

        JERRY_ASSERT (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p));

        if (curr_property_p->name_cp == property_name_cp
            && ECMA_PROPERTY_GET_NAME_TYPE (curr_property_p) == prop_name_type)
        {
#if ENABLED (JERRY_LCACHE)
          if (!ecma_is_property_lcached (curr_property_p))
          {
            ecma_lcache_insert (obj_p, property_name_cp, index_list_p[i], curr_property_p);
          }
#endif /* !ENABLED (JERRY_LCACHE) */

          return curr_property_p;
        }
      }
    }

    return NULL;
  }

  for (uint8_t i = 0; i < bucket_p->count; i++)
  {
    ecma_property_index_t property_index = index_list_p[i];

    if (property_index != 0)
    {
      ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + property_index;

      JERRY_ASSERT (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p));

      if (ECMA_PROPERTY_GET_NAME_TYPE (curr_property_p) == ECMA_DIRECT_STRING_PTR)
      {
        ecma_string_t *prop_name_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t, curr_property_p->name_cp);

        if (ecma_compare_ecma_non_direct_strings (prop_name_p, name_p))
        {
#if ENABLED (JERRY_LCACHE)
          if (!ecma_is_property_lcached (curr_property_p))
          {
            ecma_lcache_insert (obj_p, curr_property_p->name_cp, index_list_p[i], curr_property_p);
          }
#endif /* !ENABLED (JERRY_LCACHE) */

          return curr_property_p;
        }
      }
    }
  }

  return NULL;
}

#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

/**
 * @}
 * @}
 */
