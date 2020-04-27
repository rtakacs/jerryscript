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
 * Size of an entry.
 */
#define ECMA_HASHMAP_CHUNK_SIZE JMEM_ALIGNMENT

/**
 * Number of items in a chunk.
 *
 * cpointer: 4
 * cpointer-32: 2
 */
#define ECMA_HASHMAP_GROWTH_FACTOR (ECMA_HASHMAP_CHUNK_SIZE / sizeof (ecma_property_index_t))

/**
 * Compute the total size of the property hashmap.
 */
#define ECMA_HASHMAP_GET_SIZE(bucket_count) \
  (sizeof (ecma_hashmap_header_t) + (bucket_count) * sizeof (jmem_cpointer_t))

/**
 * Size of the bucket list.
 */
#define ECMA_HASHMAP_GET_BUCKET_SIZE(capacity) \
  (sizeof (ecma_hashmap_bucket_header_t) + (capacity) * sizeof (ecma_property_index_t))

/**
 * Insert into bucket.
 */
static void
ecma_property_hashmap_insert_into_bucket (jmem_cpointer_t *bucket_p, /**< bucket pointer */
                                          ecma_property_index_t index) /**< property index */
{
  ecma_hashmap_bucket_header_t *bucket_header_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_bucket_header_t, *bucket_p);

  if (JERRY_UNLIKELY(bucket_header_p->count == bucket_header_p->capacity))
  {
    size_t old_size = ECMA_HASHMAP_GET_BUCKET_SIZE (bucket_header_p->capacity);
    size_t new_size = old_size + ECMA_HASHMAP_CHUNK_SIZE;

    bucket_header_p = jmem_heap_realloc_block (bucket_header_p, old_size, new_size);
    ECMA_SET_NON_NULL_POINTER (*bucket_p, bucket_header_p);

    bucket_header_p->capacity = (ecma_property_index_t)(bucket_header_p->capacity + ECMA_HASHMAP_GROWTH_FACTOR);
  }

  ecma_property_index_t *bucket_entry_start_p = (ecma_property_index_t *)(bucket_header_p + 1);
  // Append property to the end of the list.
  bucket_entry_start_p[bucket_header_p->count++] = index;
} /* ecma_property_hashmap_insert_into_bucket */

/**
 * Create a new property hashmap for the object.
 * The object must not has a property hashmap.
 */
void
ecma_property_hashmap_create (ecma_property_header_t *property_header_p) /**< object */
{
  JERRY_ASSERT (property_header_p != NULL);
  JERRY_ASSERT (property_header_p->cache[0] != 0);
  JERRY_ASSERT (property_header_p->count >= ECMA_PROPERTY_HASMAP_MINIMUM_SIZE);

  if (JERRY_CONTEXT (ecma_prop_hashmap_alloc_state) != ECMA_PROP_HASHMAP_ALLOC_ON)
  {
    return;
  }

  /* Let bucket count is property count / 4. */
  ecma_property_index_t bucket_count = property_header_p->count >> 2;

  /* If the bucket count is not power of two, round it to the previous power of two number.
   * Note: this can happen if fast arrays are converted to notmal objets. */
  if (JERRY_UNLIKELY ((bucket_count & (bucket_count - 1)) != 0))
  {
    bucket_count--;
    bucket_count |= bucket_count >> 1;
    bucket_count |= bucket_count >> 2;
    bucket_count |= bucket_count >> 4;
    bucket_count |= bucket_count >> 8;
#if ENABLED (JERRY_CPOINTER_32_BIT)
    bucket_count |= bucket_count >> 16;
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */

    bucket_count = (ecma_property_index_t) (bucket_count - (bucket_count >> 1));
  }

  size_t total_size = ECMA_HASHMAP_GET_SIZE (bucket_count);
  ecma_hashmap_header_t *hashmap_p = jmem_heap_alloc_block_null_on_error (total_size);

  if (hashmap_p == NULL)
  {
    return;
  }

  memset (hashmap_p, 0, total_size);
  hashmap_p->bucket_count = bucket_count;
  hashmap_p->property_count = property_header_p->count;

  /* Mark hashmap on the property list. */
  property_header_p->cache[0] = 0;
  ECMA_SET_NON_NULL_POINTER (property_header_p->cache[1], hashmap_p);

  jmem_cpointer_t *buckets_p = (jmem_cpointer_t *)(hashmap_p + 1);
  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);

  /* Initialize buckets. */
  for (uint32_t i = 0; i < bucket_count; i++)
  {
    size_t alloc_size = sizeof (ecma_hashmap_bucket_header_t) + ECMA_HASHMAP_CHUNK_SIZE;

    ecma_hashmap_bucket_header_t *bucket_header_p = jmem_heap_alloc_block (alloc_size);
    memset (bucket_header_p, 0, alloc_size);

    ECMA_SET_NON_NULL_POINTER (buckets_p[i], bucket_header_p);
    bucket_header_p->capacity = ECMA_HASHMAP_GROWTH_FACTOR;
  }

  for (ecma_property_index_t index = 0; index < property_header_p->count; index++)
  {
    ecma_property_t *curr_property_p = property_start_p + index;

    if (ECMA_PROPERTY_IS_NAMED_PROPERTY (curr_property_p))
    {
      uint32_t hash = ecma_string_get_property_name_hash (curr_property_p);
      uint32_t bucket_index = (uint32_t) (hash & (hashmap_p->bucket_count - 1u));

      ecma_property_hashmap_insert_into_bucket (&buckets_p[bucket_index], (ecma_property_index_t)(index + 1u));
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
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                property_header_p->cache[1]);

  jmem_cpointer_t *buckets_p = (jmem_cpointer_t *)(hashmap_p + 1);

  for (uint32_t i = 0; i < hashmap_p->bucket_count; i++)
  {
    jmem_cpointer_t bucket_cp = buckets_p[i];

    if (bucket_cp != JMEM_CP_NULL)
    {
      ecma_hashmap_bucket_header_t *bucket_header_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_bucket_header_t,
                                                                                 bucket_cp);

      jmem_heap_free_block (bucket_header_p, ECMA_HASHMAP_GET_BUCKET_SIZE (bucket_header_p->capacity));
    }
  }

  /* Restore the local cache. */
  for (uint32_t i = 0; i < ECMA_PROPERTY_CACHE_SIZE; i++)
  {
    property_header_p->cache[i] = 1;
  }

  jmem_heap_free_block (hashmap_p, ECMA_HASHMAP_GET_SIZE (hashmap_p->bucket_count));
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

  /* If the object has more than two times properties, create a new ahsh table. */ 
  if (property_header_p->count == hashmap_p->property_count << 1)
  {
    ecma_property_hashmap_free (property_header_p);
    ecma_property_hashmap_create (property_header_p);
    return;
  }

  uint32_t hash = ecma_string_hash (name_p);
  uint32_t bucket_index = (uint32_t)(hash & (hashmap_p->bucket_count - 1u));

  jmem_cpointer_t *buckets_p = (jmem_cpointer_t *)(hashmap_p + 1);
  jmem_cpointer_t *bucket_p = &buckets_p[bucket_index];

  ecma_property_hashmap_insert_into_bucket (bucket_p, index);
}

ecma_property_hashmap_delete_status
ecma_property_hashmap_delete (ecma_property_header_t *property_header_p, /**< object */
                              ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (property_header_p->cache[0] == 0);

  ecma_hashmap_header_t *hashmap_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_header_t,
                                                                (jmem_cpointer_t) property_header_p->cache[1]);

  uint32_t hash = ecma_string_get_property_name_hash (property_p);
  uint32_t bucket_index = (uint32_t)(hash & (hashmap_p->bucket_count - 1u));

  jmem_cpointer_t *buckets_p = (jmem_cpointer_t *)(hashmap_p + 1);
  jmem_cpointer_t bucket_cp = buckets_p[bucket_index];

  if (bucket_cp == JMEM_CP_NULL)
  {
    return ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
  }

  ecma_hashmap_bucket_header_t *bucket_header_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_bucket_header_t, bucket_cp);
  ecma_property_index_t *index_list_p = (ecma_property_index_t *)(bucket_header_p + 1);

  for (uint8_t i = 0; i < bucket_header_p->count; i++)
  {
    ecma_property_index_t property_index = index_list_p[i];

    if (property_index != 0)
    {
      ecma_property_t *curr_property_p = (ecma_property_t *) property_header_p + property_index;

      if (curr_property_p == property_p)
      {
        index_list_p[i] = 0;

        bucket_header_p->count--;
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
  uint32_t bucket_index = (uint32_t)(hash & (hashmap_p->bucket_count - 1u));

  jmem_cpointer_t *buckets_p = (jmem_cpointer_t *)(hashmap_p + 1);
  jmem_cpointer_t bucket_cp = buckets_p[bucket_index];

  ecma_hashmap_bucket_header_t *bucket_header_p = ECMA_GET_NON_NULL_POINTER (ecma_hashmap_bucket_header_t, bucket_cp);
  ecma_property_index_t *index_list_p = (ecma_property_index_t *)(bucket_header_p + 1);

  if (ECMA_IS_DIRECT_STRING (name_p))
  {
    uint8_t prop_name_type = (uint8_t) ECMA_GET_DIRECT_STRING_TYPE (name_p);
    jmem_cpointer_t property_name_cp = (jmem_cpointer_t) ECMA_GET_DIRECT_STRING_VALUE (name_p);

    JERRY_ASSERT (prop_name_type > 0);

    for (uint8_t i = 0; i < bucket_header_p->count; i++)
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
            ecma_lcache_insert (obj_p, property_name_cp, property_index, curr_property_p);
          }
#endif /* !ENABLED (JERRY_LCACHE) */

          return curr_property_p;
        }
      }
    }

    return NULL;
  }

  for (uint8_t i = 0; i < bucket_header_p->count; i++)
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
            ecma_lcache_insert (obj_p, curr_property_p->name_cp, property_index, curr_property_p);
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
