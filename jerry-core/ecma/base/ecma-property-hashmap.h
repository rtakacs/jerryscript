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

#ifndef ECMA_PROPERTY_HASHMAP_H
#define ECMA_PROPERTY_HASHMAP_H

/** \addtogroup ecma ECMA
 * @{
 *
 * \addtogroup ecmapropertyhashmap Property hashmap
 * @{
 */

/**
 * Hashmap buckets.
 */
typedef struct
{
  ecma_property_index_t bucket_count; /**< number of buckets */
  ecma_property_index_t property_count; /**<unused */
} ecma_hashmap_header_t;

/**
 * Hashmap bucket entry.
 */
typedef struct
{
  ecma_property_index_t index; /**< property index */
  jmem_cpointer_t next_cp; /**< next entry pointer */
} ecma_hashmap_entry_t;

/**
 * Hashmap bucket header.
 */
typedef struct
{
  ecma_property_index_t count; /**< property counter */
  jmem_cpointer_t next_cp; /**< next entry pointer */
} ecma_hashmap_bucket_header_t;


#if ENABLED (JERRY_PROPRETY_HASHMAP)

/* Recommended minimum number of items in a property cache. */
#define ECMA_PROPERTY_HASMAP_MINIMUM_SIZE 32

/**
 * Simple ecma values
 */
typedef enum
{
  ECMA_PROPERTY_HASHMAP_DELETE_NO_HASHMAP, /**< object has no hashmap */
  ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP, /**< object has hashmap */
  ECMA_PROPERTY_HASHMAP_DELETE_RECREATE_HASHMAP, /**< hashmap should be recreated */
} ecma_property_hashmap_delete_status;

void ecma_property_hashmap_create (ecma_property_header_t *property_header_p);
void ecma_property_hashmap_free (ecma_property_header_t *property_header_p);
void ecma_property_hashmap_insert (ecma_property_header_t *property_header_p,
                                   ecma_string_t *name_p, ecma_property_index_t index);
ecma_property_hashmap_delete_status ecma_property_hashmap_delete (ecma_property_header_t *property_header_p,
                                                                  ecma_property_t *property_p);

ecma_property_t *ecma_property_hashmap_find (ecma_property_header_t *property_header_p, ecma_string_t *name_p,
                                             jmem_cpointer_t *property_real_name_cp, ecma_property_index_t *index);
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

/**
 * @}
 * @}
 */

#endif /* !ECMA_PROPERTY_HASHMAP_H */
