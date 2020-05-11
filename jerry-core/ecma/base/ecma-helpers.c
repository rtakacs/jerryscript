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

#include "ecma-alloc.h"
#include "ecma-array-object.h"
#include "ecma-gc.h"
#include "ecma-globals.h"
#include "ecma-helpers.h"
#include "ecma-lcache.h"
#include "ecma-property-hashmap.h"
#include "jcontext.h"
#include "jrt-bit-fields.h"
#include "byte-code.h"
#include "re-compiler.h"
#include "ecma-builtins.h"

#if ENABLED (JERRY_DEBUGGER)
#include "debugger.h"
#endif /* ENABLED (JERRY_DEBUGGER) */

/** \addtogroup ecma ECMA
 * @{
 *
 * \addtogroup ecmahelpers Helpers for operations with ECMA data types
 * @{
 */

JERRY_STATIC_ASSERT (ECMA_PROPERTY_TYPE_MASK >= ECMA_PROPERTY_TYPE__MAX,
                     ecma_property_types_must_be_lower_than_the_container_mask);

JERRY_STATIC_ASSERT (ECMA_OBJECT_TYPE_MASK >= ECMA_OBJECT_TYPE__MAX - 1,
                     ecma_object_types_must_be_lower_than_the_container_mask);

JERRY_STATIC_ASSERT (ECMA_OBJECT_TYPE_MASK >= ECMA_LEXICAL_ENVIRONMENT_TYPE__MAX,
                     ecma_lexical_environment_types_must_be_lower_than_the_container_mask);

JERRY_STATIC_ASSERT (ECMA_OBJECT_TYPE_MASK + 1 == ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV,
                     ecma_built_in_flag_must_follow_the_object_type);

JERRY_STATIC_ASSERT (ECMA_OBJECT_FLAG_EXTENSIBLE == (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV << 1),
                     ecma_extensible_flag_must_follow_the_built_in_flag);

JERRY_STATIC_ASSERT (ECMA_OBJECT_REF_ONE == (ECMA_OBJECT_FLAG_EXTENSIBLE << 1),
                     ecma_object_ref_one_must_follow_the_extensible_flag);

JERRY_STATIC_ASSERT (((ECMA_OBJECT_MAX_REF + ECMA_OBJECT_REF_ONE) | (ECMA_OBJECT_REF_ONE - 1)) == UINT16_MAX,
                      ecma_object_max_ref_does_not_fill_the_remaining_bits);

JERRY_STATIC_ASSERT (ECMA_PROPERTY_TYPE_DELETED == (ECMA_DIRECT_STRING_MAGIC << ECMA_PROPERTY_NAME_TYPE_SHIFT),
                     ecma_property_type_deleted_must_have_magic_string_name_type);

/**
 * Create an object with specified prototype object
 * (or NULL prototype if there is not prototype for the object)
 * and value of 'Extensible' attribute.
 *
 * Reference counter's value will be set to one.
 *
 * @return pointer to the object's descriptor
 */
ecma_object_t *
ecma_create_object (ecma_object_t *prototype_object_p, /**< pointer to prototybe of the object (or NULL) */
                    size_t ext_object_size, /**< size of extended objects */
                    ecma_object_type_t type) /**< object type */
{
  ecma_object_t *new_object_p;

  if (ext_object_size > 0)
  {
    new_object_p = (ecma_object_t *) ecma_alloc_extended_object (ext_object_size);
  }
  else
  {
    new_object_p = ecma_alloc_object ();
  }

  new_object_p->type_flags_refs = (uint16_t) (type | ECMA_OBJECT_FLAG_EXTENSIBLE);

  ecma_init_gc_info (new_object_p);

  new_object_p->u1.property_header_cp = JMEM_CP_NULL;

  ECMA_SET_POINTER (new_object_p->u2.prototype_cp, prototype_object_p);

  return new_object_p;
} /* ecma_create_object */

/**
 * Create a declarative lexical environment with specified outer lexical environment
 * (or NULL if the environment is not nested).
 *
 * See also: ECMA-262 v5, 10.2.1.1
 *
 * Reference counter's value will be set to one.
 *
 * @return pointer to the descriptor of lexical environment
 */
ecma_object_t *
ecma_create_decl_lex_env (ecma_object_t *outer_lexical_environment_p) /**< outer lexical environment */
{
  ecma_object_t *new_lexical_environment_p = ecma_alloc_object ();

  uint16_t type = ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_LEXICAL_ENVIRONMENT_DECLARATIVE;
  new_lexical_environment_p->type_flags_refs = type;

  ecma_init_gc_info (new_lexical_environment_p);

  new_lexical_environment_p->u1.property_header_cp = JMEM_CP_NULL;

  ECMA_SET_POINTER (new_lexical_environment_p->u2.outer_reference_cp, outer_lexical_environment_p);

  return new_lexical_environment_p;
} /* ecma_create_decl_lex_env */

/**
 * Create a object lexical environment with specified outer lexical environment
 * (or NULL if the environment is not nested), binding object and provided type flag.
 *
 * See also: ECMA-262 v5, 10.2.1.2
 *
 * Reference counter's value will be set to one.
 *
 * @return pointer to the descriptor of lexical environment
 */
ecma_object_t *
ecma_create_object_lex_env (ecma_object_t *outer_lexical_environment_p, /**< outer lexical environment */
                            ecma_object_t *binding_obj_p, /**< binding object */
                            ecma_lexical_environment_type_t type) /**< type of the new lexical environment */
{
#if ENABLED (JERRY_ES2015)
  JERRY_ASSERT (type == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND
                || type == ECMA_LEXICAL_ENVIRONMENT_HOME_OBJECT_BOUND);
#else /* !ENABLED (JERRY_ES2015) */
  JERRY_ASSERT (type == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND);
#endif /* ENABLED (JERRY_ES2015) */

  JERRY_ASSERT (binding_obj_p != NULL
                && !ecma_is_lexical_environment (binding_obj_p));

  ecma_object_t *new_lexical_environment_p = ecma_alloc_object ();

  new_lexical_environment_p->type_flags_refs = (uint16_t) (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | type);

  ecma_init_gc_info (new_lexical_environment_p);

  ECMA_SET_NON_NULL_POINTER (new_lexical_environment_p->u1.bound_object_cp,
                             binding_obj_p);

  ECMA_SET_POINTER (new_lexical_environment_p->u2.outer_reference_cp, outer_lexical_environment_p);

  return new_lexical_environment_p;
} /* ecma_create_object_lex_env */

/**
 * Check if the object is lexical environment.
 *
 * @return true  - if object is a lexical environment
 *         false - otherwise
 */
inline bool JERRY_ATTR_PURE
ecma_is_lexical_environment (const ecma_object_t *object_p) /**< object or lexical environment */
{
  JERRY_ASSERT (object_p != NULL);

  uint32_t full_type = object_p->type_flags_refs & (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_OBJECT_TYPE_MASK);

  return full_type >= (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_LEXICAL_ENVIRONMENT_TYPE_START);
} /* ecma_is_lexical_environment */

/**
 * Set value of [[Extensible]] object's internal property.
 */
inline void
ecma_op_ordinary_object_set_extensible (ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  object_p->type_flags_refs = (uint16_t) (object_p->type_flags_refs | ECMA_OBJECT_FLAG_EXTENSIBLE);
} /* ecma_op_ordinary_object_set_extensible */

/**
 * Get object's internal implementation-defined type.
 *
 * @return type of the object (ecma_object_type_t)
 */
inline ecma_object_type_t JERRY_ATTR_PURE
ecma_get_object_type (const ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  return (ecma_object_type_t) (object_p->type_flags_refs & ECMA_OBJECT_TYPE_MASK);
} /* ecma_get_object_type */

/**
 * Check if the object is a built-in object
 *
 * @return true  - if object is a built-in object
 *         false - otherwise
 */
inline bool JERRY_ATTR_PURE
ecma_get_object_is_builtin (const ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  return (object_p->type_flags_refs & ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV) != 0;
} /* ecma_get_object_is_builtin */

/**
 * Set flag indicating whether the object is a built-in object
 */
inline void
ecma_set_object_is_builtin (ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!(object_p->type_flags_refs & ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV));
  JERRY_ASSERT ((object_p->type_flags_refs & ECMA_OBJECT_TYPE_MASK) < ECMA_LEXICAL_ENVIRONMENT_TYPE_START);

  object_p->type_flags_refs = (uint16_t) (object_p->type_flags_refs | ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV);
} /* ecma_set_object_is_builtin */

/**
 * Get the built-in ID of the object.
 * If the object is not builtin, return ECMA_BUILTIN_ID__COUNT
 *
 * @return the ID of the built-in
 */
inline uint8_t
ecma_get_object_builtin_id (ecma_object_t *object_p) /**< object */
{
  if (!ecma_get_object_is_builtin (object_p))
  {
    return ECMA_BUILTIN_ID__COUNT;
  }

  ecma_built_in_props_t *built_in_props_p;
  ecma_object_type_t object_type = ecma_get_object_type (object_p);

  if (object_type == ECMA_OBJECT_TYPE_CLASS || object_type == ECMA_OBJECT_TYPE_ARRAY)
  {
    built_in_props_p = &((ecma_extended_built_in_object_t *) object_p)->built_in;
  }
  else
  {
    built_in_props_p = &((ecma_extended_object_t *) object_p)->u.built_in;
  }

  return built_in_props_p->id;
} /* ecma_get_object_builtin_id */

/**
 * Get type of lexical environment.
 *
 * @return type of the lexical environment (ecma_lexical_environment_type_t)
 */
inline ecma_lexical_environment_type_t JERRY_ATTR_PURE
ecma_get_lex_env_type (const ecma_object_t *object_p) /**< lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p));

  return (ecma_lexical_environment_type_t) (object_p->type_flags_refs & ECMA_OBJECT_TYPE_MASK);
} /* ecma_get_lex_env_type */

/**
 * Get lexical environment's bound object.
 *
 * @return pointer to ecma object
 */
inline ecma_object_t *JERRY_ATTR_PURE
ecma_get_lex_env_binding_object (const ecma_object_t *object_p) /**< object-bound lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p));
#if ENABLED (JERRY_ES2015)
  JERRY_ASSERT (ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND
                || ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_HOME_OBJECT_BOUND);
#else /* !ENABLED (JERRY_ES2015) */
  JERRY_ASSERT (ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND);
#endif /* ENABLED (JERRY_ES2015) */

  return ECMA_GET_NON_NULL_POINTER (ecma_object_t, object_p->u1.bound_object_cp);
} /* ecma_get_lex_env_binding_object */

/**
 * Create a new lexical environment with the same property list as the passed lexical environment
 *
 * @return pointer to the newly created lexical environment
 */
ecma_object_t *
ecma_clone_decl_lexical_environment (ecma_object_t *lex_env_p, /**< declarative lexical environment */
                                     bool copy_values) /**< copy property values as well */
{
  JERRY_ASSERT (ecma_get_lex_env_type (lex_env_p) == ECMA_LEXICAL_ENVIRONMENT_DECLARATIVE);
  JERRY_ASSERT (lex_env_p->u2.outer_reference_cp != JMEM_CP_NULL);

  ecma_object_t *outer_lex_env_p = ECMA_GET_NON_NULL_POINTER (ecma_object_t, lex_env_p->u2.outer_reference_cp);
  ecma_object_t *new_lex_env_p = ecma_create_decl_lex_env (outer_lex_env_p);

  jmem_cpointer_t prop_iter_cp = lex_env_p->u1.property_header_cp;
  JERRY_ASSERT (prop_iter_cp != JMEM_CP_NULL);

  ecma_property_header_t *property_header_p = ECMA_GET_NON_NULL_POINTER (ecma_property_header_t, prop_iter_cp);
  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
  ecma_property_index_t property_count = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

  for (ecma_property_index_t i = 0; i < property_count; i++)
  {
    ecma_property_t *property_p = property_start_p + i;

    JERRY_ASSERT (ECMA_PROPERTY_IS_PROPERTY (property_p));

    if (property_p->type_flags != ECMA_PROPERTY_TYPE_DELETED)
    {
      JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA);

      uint8_t prop_attributes = (uint8_t) (property_p->type_flags & ECMA_PROPERTY_CONFIGURABLE_ENUMERABLE_WRITABLE);
      ecma_string_t *name_p = ecma_string_from_property_name (property_p);

      ecma_property_t *new_property_p = ecma_create_named_data_property (new_lex_env_p, name_p, prop_attributes);

      ecma_deref_ecma_string (name_p);

      JERRY_ASSERT (new_property_p->u.value == ECMA_VALUE_UNDEFINED);

      if (copy_values)
      {
        new_property_p->u.value = ecma_copy_value_if_not_object (property_p->u.value);
      }
      else
      {
        new_property_p->u.value = ECMA_VALUE_UNINITIALIZED;
      }
    }
  }

  ecma_deref_object (lex_env_p);
  return new_lex_env_p;
} /* ecma_clone_decl_lexical_environment */

/**
 * Create a property in an object and link it into
 * the object's properties.
 *
 * @return pointer to the newly created property value
 */
static ecma_property_t *
ecma_create_property (ecma_object_t *object_p, /**< the object */
                      ecma_string_t *name_p, /**< property name */
                      uint8_t type_and_flags, /**< type and flags, see ecma_property_info_t */
                      ecma_property_value_t value) /**< property value */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (name_p != NULL);

  ecma_property_header_t *property_header_p = NULL;

  if (JERRY_UNLIKELY (object_p->u1.property_header_cp == JMEM_CP_NULL))
  {
    property_header_p = ecma_alloc_property_list (1);
  }
  else
  {
    property_header_p = ecma_realloc_property_list (ECMA_GET_NON_NULL_POINTER (ecma_property_header_t,
                                                                               object_p->u1.property_header_cp));

#if ENABLED (JERRY_LCACHE)
    ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
    ecma_property_index_t loop_cnt = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

    /* Update the memory addresses of the properties. */
    for (uint32_t i = 0; i < loop_cnt - 1u; i++)
    {
      ecma_property_t *property_p = property_start_p + i;

      if (property_p->type_flags != ECMA_PROPERTY_TYPE_DELETED)
      {
        if (ecma_is_property_lcached (property_p))
        {
          ecma_lcache_hash_entry_t *entry_p = JERRY_CONTEXT (lcache)[0] + property_p->lcache_id;
          entry_p->prop_p = property_p;
        }
      }
    }
#endif /* ENABLED (JERRY_LCACHE) */
  }

  ecma_property_index_t index = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);
  ecma_property_t *property_p = ((ecma_property_t *) property_header_p) + index;

  uint8_t name_type;
  property_p->name_cp = ecma_string_to_property_name (name_p, &name_type);
  property_p->type_flags = (uint8_t) (type_and_flags | name_type);
  property_p->u = value;
  property_p->lcache_id = 0;

  JMEM_CP_SET_NON_NULL_POINTER (object_p->u1.property_header_cp, property_header_p);

#if ENABLED (JERRY_PROPRETY_HASHMAP)
  if (property_header_p->cache[0] == 0)
  {
    ecma_property_hashmap_insert (property_header_p, name_p, index);
  }
  else
  {
    if (index >= ECMA_PROPERTY_HASMAP_MINIMUM_SIZE)
    {
      ecma_property_hashmap_create (property_header_p);
    }
  }
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

  return property_p;
} /* ecma_create_property */

/**
 * Create named data property with given name, attributes and undefined value
 * in the specified object.
 *
 * @return pointer to the newly created property
 */
ecma_property_t *
ecma_create_named_data_property (ecma_object_t *object_p, /**< object */
                                 ecma_string_t *name_p, /**< property name */
                                 uint8_t prop_attributes) /**< property attributes (See: ecma_property_flags_t) */

{
  JERRY_ASSERT (object_p != NULL && name_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p)
                || !ecma_op_object_is_fast_array (object_p));
  JERRY_ASSERT (ecma_find_named_property (object_p, name_p) == NULL);
  JERRY_ASSERT ((prop_attributes & ~ECMA_PROPERTY_CONFIGURABLE_ENUMERABLE_WRITABLE) == 0);

  uint8_t type_and_flags = ECMA_PROPERTY_TYPE_NAMEDDATA | prop_attributes;

  ecma_property_value_t value;
  value.value = ECMA_VALUE_UNDEFINED;

  return ecma_create_property (object_p, name_p, type_and_flags, value);
} /* ecma_create_named_data_property */

/**
 * Create named accessor property with given name, attributes, getter and setter.
 *
 * @return pointer to the newly created property
 */
ecma_property_t *
ecma_create_named_accessor_property (ecma_object_t *object_p, /**< object */
                                     ecma_string_t *name_p, /**< property name */
                                     ecma_object_t *get_p, /**< getter */
                                     ecma_object_t *set_p, /**< setter */
                                     uint8_t prop_attributes) /**< property attributes */
{
  JERRY_ASSERT (object_p != NULL && name_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p)
                || !ecma_op_object_is_fast_array (object_p));
  JERRY_ASSERT (ecma_find_named_property (object_p, name_p) == NULL);
  JERRY_ASSERT ((prop_attributes & ~ECMA_PROPERTY_CONFIGURABLE_ENUMERABLE) == 0);

  uint8_t type_and_flags = ECMA_PROPERTY_TYPE_NAMEDACCESSOR | prop_attributes;

  ecma_property_value_t value;
#if ENABLED (JERRY_CPOINTER_32_BIT)
  ecma_getter_setter_pointers_t *getter_setter_pair_p;
  getter_setter_pair_p = jmem_pools_alloc (sizeof (ecma_getter_setter_pointers_t));
  ECMA_SET_POINTER (getter_setter_pair_p->getter_cp, get_p);
  ECMA_SET_POINTER (getter_setter_pair_p->setter_cp, set_p);
  ECMA_SET_NON_NULL_POINTER (value.getter_setter_pair_cp, getter_setter_pair_p);
#else /* !ENABLED (JERRY_CPOINTER_32_BIT) */
  ECMA_SET_POINTER (value.getter_setter_pair.getter_cp, get_p);
  ECMA_SET_POINTER (value.getter_setter_pair.setter_cp, set_p);
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */

  return ecma_create_property (object_p, name_p, type_and_flags, value);
} /* ecma_create_named_accessor_property */

/**
 * Find named data property or named access property in specified object.
 *
 * @return pointer to the property, if it is found,
 *         NULL - otherwise.
 */
ecma_property_t *
ecma_find_named_property (ecma_object_t *obj_p, /**< object to find property in */
                          ecma_string_t *name_p) /**< property's name */
{
  JERRY_ASSERT (obj_p != NULL);
  JERRY_ASSERT (name_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (obj_p)
                || !ecma_op_object_is_fast_array (obj_p));

  ecma_property_t *property_p;

#if ENABLED (JERRY_LCACHE)
  property_p = ecma_lcache_lookup (obj_p, name_p);
  if (property_p != NULL)
  {
    return property_p;
  }
#endif /* ENABLED (JERRY_LCACHE) */

  if (JERRY_UNLIKELY (obj_p->u1.property_header_cp == JMEM_CP_NULL))
  {
    return NULL;
  }

  ecma_property_header_t *property_header_p = ECMA_GET_NON_NULL_POINTER (ecma_property_header_t,
                                                                         obj_p->u1.property_header_cp);
  ecma_property_t *property_list_p = (ecma_property_t *) property_header_p;

#if ENABLED (JERRY_PROPRETY_HASHMAP)
  if (property_header_p->cache[0] == 0)
  {
    ecma_property_index_t property_index = ECMA_PROPERTY_INDEX_INVALID;
    jmem_cpointer_t property_real_name_cp;

    property_p = ecma_property_hashmap_find (property_header_p,
                                             name_p,
                                             &property_real_name_cp,
                                             &property_index);

#if ENABLED (JERRY_LCACHE)
    if (property_p != NULL
        && !ecma_is_property_lcached (property_p))
    {
      ecma_lcache_insert (obj_p, property_real_name_cp, property_p);
    }
#else /* !ENABLED (JERRY_LCACHE) */
    (void) property_index;
#endif /* ENABLED (JERRY_LCACHE) */
    return property_p;
  }
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

  uint8_t prop_name_type = ECMA_DIRECT_STRING_PTR;
  jmem_cpointer_t prop_name_cp;

  if (ECMA_IS_DIRECT_STRING (name_p))
  {
    prop_name_type = (uint8_t) ECMA_GET_DIRECT_STRING_TYPE (name_p);
    prop_name_cp = (jmem_cpointer_t) ECMA_GET_DIRECT_STRING_VALUE (name_p);
  }
  else
  {
    ECMA_SET_NON_NULL_POINTER (prop_name_cp, name_p);
  }
  ecma_property_index_t prop_index = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

#if !ENABLED (JERRY_LCACHE)
  if (JERRY_LIKELY (prop_index > ECMA_PROPERTY_CACHE_SIZE))
  {
    for (uint32_t i = 0; i < ECMA_PROPERTY_CACHE_SIZE; i++)
    {
      property_p = property_list_p + property_header_p->cache[i];
      if (JERRY_LIKELY (property_p->name_cp == prop_name_cp
                        && ECMA_PROPERTY_GET_NAME_TYPE (property_p) == prop_name_type))
      {
        return property_p;
      }
    }
  }
#endif /* !ENABLED (JERRY_LCACHE) */

  property_p = property_list_p;
  ecma_property_t *property_list_end_p = property_list_p + prop_index;

  if (ECMA_IS_DIRECT_STRING (name_p))
  {
    JERRY_ASSERT (prop_name_type > 0);

    do
    {
      property_p++;

      if (property_p->name_cp == prop_name_cp && ECMA_PROPERTY_GET_NAME_TYPE (property_p) == prop_name_type)
      {
        prop_index = (ecma_property_index_t) (property_p - property_list_p);
        goto insert;
      }
    }
    while (property_p < property_list_end_p);
  }
  else
  {
    do
    {
      property_p++;

      if (ECMA_PROPERTY_GET_NAME_TYPE (property_p) == ECMA_DIRECT_STRING_PTR)
      {
        if (prop_name_cp == property_p->name_cp)
        {
          prop_index = (ecma_property_index_t) (property_p - property_list_p);
          goto insert;
        }

        ecma_string_t *prop_name_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t, property_p->name_cp);

        if (ecma_compare_ecma_non_direct_strings (name_p, prop_name_p))
        {
          prop_index = (ecma_property_index_t) (property_p - property_list_p);
          prop_name_cp = property_p->name_cp;
          goto insert;
        }
      }
    }
    while (property_p < property_list_end_p);
  }

  return NULL;

insert:
  JERRY_ASSERT (prop_index != 0);

#if ENABLED (JERRY_LCACHE)
  if (!ecma_is_property_lcached (property_p))
  {
    ecma_lcache_insert (obj_p, prop_name_cp, property_p);
  }
#else /* !ENABLED (JERRY_LCACHE) */
#if !ENABLED (JERRY_CPOINTER_32_BIT)
  property_header_p->cache[2] = property_header_p->cache[1];
#endif /* !ENABLED (JERRY_CPOINTER_32_BIT) */
  property_header_p->cache[1] = property_header_p->cache[0];
  property_header_p->cache[0] = prop_index;
#endif /* ENABLED (JERRY_LCACHE) */

  return property_p;
} /* ecma_find_named_property */

/**
 * Get named data property or named access property in specified object.
 *
 * Warning:
 *         the property must exist
 *
 * @return pointer to the property, if it is found,
 *         NULL - otherwise.
 */
ecma_property_t *
ecma_get_named_data_property (ecma_object_t *obj_p, /**< object to find property in */
                              ecma_string_t *name_p) /**< property's name */
{
  JERRY_ASSERT (obj_p != NULL);
  JERRY_ASSERT (name_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (obj_p)
                || !ecma_op_object_is_fast_array (obj_p));

  ecma_property_t *property_p = ecma_find_named_property (obj_p, name_p);

  JERRY_ASSERT (property_p != NULL
                && ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA);

  return property_p;
} /* ecma_get_named_data_property */

/**
 * Free property values and change their type to deleted.
 */
void
ecma_free_property (ecma_object_t *object_p, /**< object the property belongs to */
                    ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (object_p != NULL && property_p != NULL);

  switch (ECMA_PROPERTY_GET_TYPE (property_p))
  {
    case ECMA_PROPERTY_TYPE_NAMEDDATA:
    {
      ecma_free_value_if_not_object (property_p->u.value);
      break;
    }
    case ECMA_PROPERTY_TYPE_NAMEDACCESSOR:
    {
#if ENABLED (JERRY_CPOINTER_32_BIT)
      ecma_getter_setter_pointers_t *getter_setter_pair_p;
      getter_setter_pair_p = ECMA_GET_NON_NULL_POINTER (ecma_getter_setter_pointers_t,
                                                        property_p->u.getter_setter_pair_cp);
      jmem_pools_free (getter_setter_pair_p, sizeof (ecma_getter_setter_pointers_t));
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */
      break;
    }
    default:
    {
      JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_INTERNAL);

      /* Must be a native pointer. */
      JERRY_ASSERT (ECMA_PROPERTY_GET_NAME_TYPE (property_p) == ECMA_DIRECT_STRING_MAGIC
                    && property_p->name_cp >= LIT_FIRST_INTERNAL_MAGIC_STRING);
      break;
    }
  }

#if ENABLED (JERRY_LCACHE)
  if (ecma_is_property_lcached (property_p))
  {
    ecma_lcache_invalidate (object_p, property_p);
  }
#endif /* ENABLED (JERRY_LCACHE) */

  if (ECMA_PROPERTY_GET_NAME_TYPE (property_p) == ECMA_DIRECT_STRING_PTR)
  {
    ecma_string_t *prop_name_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t, property_p->name_cp);
    ecma_deref_ecma_string (prop_name_p);
  }
} /* ecma_free_property */

/**
 * Delete the object's property referenced by its value pointer.
 *
 * Note: specified property must be owned by specified object.
 */
void
ecma_delete_property (ecma_object_t *object_p, /**< object */
                      ecma_property_t *property_del_p) /**< property value reference */
{
  jmem_cpointer_t cur_prop_cp = object_p->u1.property_header_cp;

  if (cur_prop_cp == JMEM_CP_NULL)
  {
    return;
  }

  ecma_property_header_t *property_header_p = ECMA_GET_NON_NULL_POINTER (ecma_property_header_t, cur_prop_cp);

#if ENABLED (JERRY_PROPRETY_HASHMAP)
  ecma_property_hashmap_delete_status hashmap_status = ECMA_PROPERTY_HASHMAP_DELETE_NO_HASHMAP;

  if (property_header_p->cache[0] == 0)
  {
    hashmap_status = ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP;
  }
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
  ecma_property_index_t property_count = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

  for (ecma_property_index_t i = 0; i < property_count; i++)
  {
    ecma_property_t *property_p = property_start_p + i;

    JERRY_ASSERT (ECMA_PROPERTY_IS_PROPERTY (property_p));

    if (property_p == property_del_p)
    {
      JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) != ECMA_PROPERTY_TYPE_SPECIAL);

#if ENABLED (JERRY_PROPRETY_HASHMAP)
      if (hashmap_status == ECMA_PROPERTY_HASHMAP_DELETE_HAS_HASHMAP)
      {
        hashmap_status = ecma_property_hashmap_delete (property_header_p, property_p);
      }
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */

      ecma_free_property (object_p, property_p);

      property_p->type_flags = ECMA_PROPERTY_TYPE_DELETED;
      property_p->name_cp = LIT_INTERNAL_MAGIC_STRING_DELETED;

#if ENABLED (JERRY_PROPRETY_HASHMAP)
      if (hashmap_status == ECMA_PROPERTY_HASHMAP_DELETE_RECREATE_HASHMAP)
      {
        ecma_property_hashmap_free (property_header_p);
        ecma_property_hashmap_create (property_header_p);
      }
#endif /* ENABLED (JERRY_PROPRETY_HASHMAP) */
      return;
    }
  }
} /* ecma_delete_property */

/**
 * Check whether the object contains a property
 */
static void
ecma_assert_object_contains_the_property (const ecma_object_t *object_p, /**< ecma-object */
                                          const ecma_property_t *prop_p, /**< property pointer */
                                          ecma_property_types_t type) /**< expected property type */
{
#ifndef JERRY_NDEBUG
  jmem_cpointer_t prop_iter_cp = object_p->u1.property_header_cp;
  JERRY_ASSERT (prop_iter_cp != JMEM_CP_NULL);

  ecma_property_header_t *property_header_p = ECMA_GET_NON_NULL_POINTER (ecma_property_header_t, prop_iter_cp);
  ecma_property_t *property_start_p = ECMA_PROPERTY_LIST_START (property_header_p);
  ecma_property_index_t property_count = ECMA_PROPERTY_LIST_PROPERTY_COUNT (property_header_p);

  for (ecma_property_index_t i = 0; i < property_count; i++)
  {
    ecma_property_t *property_p = property_start_p + i;

    if (property_p == prop_p)
    {
      JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == type);
      return;
    }
  }
#else /* JERRY_NDEBUG */
  JERRY_UNUSED (object_p);
  JERRY_UNUSED (prop_p);
  JERRY_UNUSED (type);
#endif /* !JERRY_NDEBUG */
} /* ecma_assert_object_contains_the_property */

/**
 * Assign value to named data property
 *
 * Note:
 *      value previously stored in the property is freed
 */
inline void JERRY_ATTR_ALWAYS_INLINE
ecma_named_data_property_assign_value (ecma_object_t *obj_p, /**< object */
                                       ecma_property_t *property_p, /**< property reference */
                                       ecma_value_t value) /**< value to assign */
{
  ecma_assert_object_contains_the_property (obj_p, property_p, ECMA_PROPERTY_TYPE_NAMEDDATA);

  ecma_value_assign_value (&property_p->u.value, value);
} /* ecma_named_data_property_assign_value */

/**
 * Get named accessor property getter-setter-pair
 *
 * @return pointer to object's getter-setter pair
 */
ecma_getter_setter_pointers_t *
ecma_get_named_accessor_property (const ecma_property_t *property_p) /**< property value reference */
{
#if ENABLED (JERRY_CPOINTER_32_BIT)
  return ECMA_GET_NON_NULL_POINTER (ecma_getter_setter_pointers_t, property_p->u.getter_setter_pair_cp);
#else /* !ENABLED (JERRY_CPOINTER_32_BIT) */
  return (ecma_getter_setter_pointers_t *) &property_p->u.getter_setter_pair;
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */
} /* ecma_get_named_accessor_property */

/**
 * Set getter of named accessor property
 */
void
ecma_set_named_accessor_property_getter (ecma_object_t *object_p, /**< the property's container */
                                         ecma_property_t *property_p, /**< property value reference */
                                         ecma_object_t *getter_p) /**< getter object */
{
  ecma_assert_object_contains_the_property (object_p, property_p, ECMA_PROPERTY_TYPE_NAMEDACCESSOR);

#if ENABLED (JERRY_CPOINTER_32_BIT)
  ecma_getter_setter_pointers_t *getter_setter_pair_p;
  getter_setter_pair_p = ECMA_GET_NON_NULL_POINTER (ecma_getter_setter_pointers_t,
                                                    property_p->u.getter_setter_pair_cp);
  ECMA_SET_POINTER (getter_setter_pair_p->getter_cp, getter_p);
#else /* !ENABLED (JERRY_CPOINTER_32_BIT) */
  ECMA_SET_POINTER (property_p->u.getter_setter_pair.getter_cp, getter_p);
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */
} /* ecma_set_named_accessor_property_getter */

/**
 * Set setter of named accessor property
 */
void
ecma_set_named_accessor_property_setter (ecma_object_t *object_p, /**< the property's container */
                                         ecma_property_t *property_p, /**< property reference */
                                         ecma_object_t *setter_p) /**< setter object */
{
  ecma_assert_object_contains_the_property (object_p, property_p, ECMA_PROPERTY_TYPE_NAMEDACCESSOR);

#if ENABLED (JERRY_CPOINTER_32_BIT)
  ecma_getter_setter_pointers_t *getter_setter_pair_p;
  getter_setter_pair_p = ECMA_GET_NON_NULL_POINTER (ecma_getter_setter_pointers_t,
                                                    property_p->u.getter_setter_pair_cp);
  ECMA_SET_POINTER (getter_setter_pair_p->setter_cp, setter_p);
#else /* !ENABLED (JERRY_CPOINTER_32_BIT) */
  ECMA_SET_POINTER (property_p->u.getter_setter_pair.setter_cp, setter_p);
#endif /* ENABLED (JERRY_CPOINTER_32_BIT) */
} /* ecma_set_named_accessor_property_setter */

/**
 * Get property's 'Writable' attribute value
 *
 * @return true - property is writable,
 *         false - otherwise
 */
inline bool JERRY_ATTR_ALWAYS_INLINE
ecma_is_property_writable (ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_VIRTUAL);

  return (property_p->type_flags & ECMA_PROPERTY_FLAG_WRITABLE) != 0;
} /* ecma_is_property_writable */

/**
 * Set property's 'Writable' attribute value
 */
void
ecma_set_property_writable_attr (ecma_property_t *property_p, /**< [in,out] property */
                                 bool is_writable) /**< new value for writable flag */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA);

  if (is_writable)
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags | ECMA_PROPERTY_FLAG_WRITABLE);
  }
  else
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags & ~ECMA_PROPERTY_FLAG_WRITABLE);
  }
} /* ecma_set_property_writable_attr */

/**
 * Get property's 'Enumerable' attribute value
 *
 * @return true - property is enumerable,
 *         false - otherwise
 */
inline bool JERRY_ATTR_ALWAYS_INLINE
ecma_is_property_enumerable (ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_VIRTUAL);

  return (property_p->type_flags & ECMA_PROPERTY_FLAG_ENUMERABLE) != 0;
} /* ecma_is_property_enumerable */

/**
 * Set property's 'Enumerable' attribute value
 */
void
ecma_set_property_enumerable_attr (ecma_property_t *property_p, /**< [in,out] property */
                                   bool is_enumerable) /**< new value for enumerable flag */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR);

  if (is_enumerable)
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags | ECMA_PROPERTY_FLAG_ENUMERABLE);
  }
  else
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags & ~ECMA_PROPERTY_FLAG_ENUMERABLE);
  }
} /* ecma_set_property_enumerable_attr */

/**
 * Get property's 'Configurable' attribute value
 *
 * @return true - property is configurable,
 *         false - otherwise
 */
inline bool JERRY_ATTR_ALWAYS_INLINE
ecma_is_property_configurable (ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_VIRTUAL);

  return (property_p->type_flags & ECMA_PROPERTY_FLAG_CONFIGURABLE) != 0;
} /* ecma_is_property_configurable */

/**
 * Set property's 'Configurable' attribute value
 */
void
ecma_set_property_configurable_attr (ecma_property_t *property_p, /**< [in,out] property */
                                     bool is_configurable) /**< new value for configurable flag */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR);

  if (is_configurable)
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags | ECMA_PROPERTY_FLAG_CONFIGURABLE);
  }
  else
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags & ~ECMA_PROPERTY_FLAG_CONFIGURABLE);
  }
} /* ecma_set_property_configurable_attr */

#if ENABLED (JERRY_LCACHE)

/**
 * Check whether the property is registered in LCache
 *
 * @return true / false
 */
inline bool JERRY_ATTR_ALWAYS_INLINE
ecma_is_property_lcached (ecma_property_t *property_p) /**< property */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_INTERNAL);

  return (property_p->type_flags & ECMA_PROPERTY_FLAG_LCACHED) != 0;
} /* ecma_is_property_lcached */

/**
 * Set value of flag indicating whether the property is registered in LCache
 */
inline void JERRY_ATTR_ALWAYS_INLINE
ecma_set_property_lcached (ecma_property_t *property_p, /**< property */
                           bool is_lcached) /**< new value for lcached flag */
{
  JERRY_ASSERT (ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDDATA
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_NAMEDACCESSOR
                || ECMA_PROPERTY_GET_TYPE (property_p) == ECMA_PROPERTY_TYPE_INTERNAL);

  if (is_lcached)
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags | ECMA_PROPERTY_FLAG_LCACHED);
  }
  else
  {
    property_p->type_flags = (uint8_t) (property_p->type_flags & ~ECMA_PROPERTY_FLAG_LCACHED);
  }
} /* ecma_set_property_lcached */

#endif /* ENABLED (JERRY_LCACHE) */

/**
 * Construct empty property descriptor, i.e.:
 *  property descriptor with all is_defined flags set to false and the rest - to default value.
 *
 * @return empty property descriptor
 */
ecma_property_descriptor_t
ecma_make_empty_property_descriptor (void)
{
  ecma_property_descriptor_t prop_desc;

  prop_desc.flags = 0;
  prop_desc.value = ECMA_VALUE_UNDEFINED;
  prop_desc.get_p = NULL;
  prop_desc.set_p = NULL;

  return prop_desc;
} /* ecma_make_empty_property_descriptor */

/**
 * Free values contained in the property descriptor
 * and make it empty property descriptor
 */
void
ecma_free_property_descriptor (ecma_property_descriptor_t *prop_desc_p) /**< property descriptor */
{
  if (prop_desc_p->flags & ECMA_PROP_IS_VALUE_DEFINED)
  {
    ecma_free_value (prop_desc_p->value);
  }

  if ((prop_desc_p->flags & ECMA_PROP_IS_GET_DEFINED)
      && prop_desc_p->get_p != NULL)
  {
    ecma_deref_object (prop_desc_p->get_p);
  }

  if ((prop_desc_p->flags & ECMA_PROP_IS_SET_DEFINED)
      && prop_desc_p->set_p != NULL)
  {
    ecma_deref_object (prop_desc_p->set_p);
  }

  *prop_desc_p = ecma_make_empty_property_descriptor ();
} /* ecma_free_property_descriptor */

/**
 * The size of error reference must be 8 bytes to use jmem_pools_alloc().
 */
JERRY_STATIC_ASSERT (sizeof (ecma_error_reference_t) == 8,
                     ecma_error_reference_size_must_be_8_bytes);

/**
 * Create an error reference from a given value.
 *
 * Note:
 *   Reference of the value is taken.
 *
 * @return error reference value
 */
ecma_value_t
ecma_create_error_reference (ecma_value_t value, /**< referenced value */
                             bool is_exception) /**< error reference is an exception */
{
  ecma_error_reference_t *error_ref_p = (ecma_error_reference_t *) jmem_pools_alloc (sizeof (ecma_error_reference_t));

  error_ref_p->refs_and_flags = ECMA_ERROR_REF_ONE | (is_exception ? 0 : ECMA_ERROR_REF_ABORT);
  error_ref_p->value = value;
  return ecma_make_error_reference_value (error_ref_p);
} /* ecma_create_error_reference */

/**
 * Create an error reference from the currently thrown error value.
 *
 * @return error reference value
 */
ecma_value_t
ecma_create_error_reference_from_context (void)
{
  bool is_abort = jcontext_has_pending_abort ();

  if (is_abort)
  {
    jcontext_set_abort_flag (false);
  }
  return ecma_create_error_reference (jcontext_take_exception (), !is_abort);
} /* ecma_create_error_reference_from_context */

/**
 * Create an error reference from a given object.
 *
 * Note:
 *   Reference of the value is taken.
 *
 * @return error reference value
 */
inline ecma_value_t JERRY_ATTR_ALWAYS_INLINE
ecma_create_error_object_reference (ecma_object_t *object_p) /**< referenced object */
{
  return ecma_create_error_reference (ecma_make_object_value (object_p), true);
} /* ecma_create_error_object_reference */

/**
 * Increase ref count of an error reference.
 */
void
ecma_ref_error_reference (ecma_error_reference_t *error_ref_p) /**< error reference */
{
  if (JERRY_LIKELY (error_ref_p->refs_and_flags < ECMA_ERROR_MAX_REF))
  {
    error_ref_p->refs_and_flags += ECMA_ERROR_REF_ONE;
  }
  else
  {
    jerry_fatal (ERR_REF_COUNT_LIMIT);
  }
} /* ecma_ref_error_reference */

/**
 * Decrease ref count of an error reference.
 */
void
ecma_deref_error_reference (ecma_error_reference_t *error_ref_p) /**< error reference */
{
  JERRY_ASSERT (error_ref_p->refs_and_flags >= ECMA_ERROR_REF_ONE);

  error_ref_p->refs_and_flags -= ECMA_ERROR_REF_ONE;

  if (error_ref_p->refs_and_flags < ECMA_ERROR_REF_ONE)
  {
    ecma_free_value (error_ref_p->value);
    jmem_pools_free (error_ref_p, sizeof (ecma_error_reference_t));
  }
} /* ecma_deref_error_reference */

/**
 * Raise error from the given error reference.
 *
 * Note: the error reference's ref count is also decreased
 */
void
ecma_raise_error_from_error_reference (ecma_value_t value) /**< error reference */
{
  JERRY_ASSERT (!jcontext_has_pending_exception () && !jcontext_has_pending_abort ());
  ecma_error_reference_t *error_ref_p = ecma_get_error_reference_from_value (value);

  JERRY_ASSERT (error_ref_p->refs_and_flags >= ECMA_ERROR_REF_ONE);

  ecma_value_t referenced_value = error_ref_p->value;

  jcontext_set_exception_flag (true);
  jcontext_set_abort_flag (error_ref_p->refs_and_flags & ECMA_ERROR_REF_ABORT);

  if (error_ref_p->refs_and_flags >= 2 * ECMA_ERROR_REF_ONE)
  {
    error_ref_p->refs_and_flags -= ECMA_ERROR_REF_ONE;
    referenced_value = ecma_copy_value (referenced_value);
  }
  else
  {
    jmem_pools_free (error_ref_p, sizeof (ecma_error_reference_t));
  }

  JERRY_CONTEXT (error_value) = referenced_value;
} /* ecma_raise_error_from_error_reference */

/**
 * Increase reference counter of Compact
 * Byte Code or regexp byte code.
 */
void
ecma_bytecode_ref (ecma_compiled_code_t *bytecode_p) /**< byte code pointer */
{
  /* Abort program if maximum reference number is reached. */
  if (bytecode_p->refs >= UINT16_MAX)
  {
    jerry_fatal (ERR_REF_COUNT_LIMIT);
  }

  bytecode_p->refs++;
} /* ecma_bytecode_ref */

/**
 * Decrease reference counter of Compact
 * Byte Code or regexp byte code.
 */
void
ecma_bytecode_deref (ecma_compiled_code_t *bytecode_p) /**< byte code pointer */
{
  JERRY_ASSERT (bytecode_p->refs > 0);
  JERRY_ASSERT (!(bytecode_p->status_flags & CBC_CODE_FLAGS_STATIC_FUNCTION));

  bytecode_p->refs--;

  if (bytecode_p->refs > 0)
  {
    /* Non-zero reference counter. */
    return;
  }

  if (bytecode_p->status_flags & CBC_CODE_FLAGS_FUNCTION)
  {
    ecma_value_t *literal_start_p = NULL;
    uint32_t literal_end;
    uint32_t const_literal_end;

    if (bytecode_p->status_flags & CBC_CODE_FLAGS_UINT16_ARGUMENTS)
    {
      cbc_uint16_arguments_t *args_p = (cbc_uint16_arguments_t *) bytecode_p;
      literal_end = args_p->literal_end;
      const_literal_end = args_p->const_literal_end;

      literal_start_p = (ecma_value_t *) ((uint8_t *) bytecode_p + sizeof (cbc_uint16_arguments_t));
      literal_start_p -= args_p->register_end;
    }
    else
    {
      cbc_uint8_arguments_t *args_p = (cbc_uint8_arguments_t *) bytecode_p;
      literal_end = args_p->literal_end;
      const_literal_end = args_p->const_literal_end;

      literal_start_p = (ecma_value_t *) ((uint8_t *) bytecode_p + sizeof (cbc_uint8_arguments_t));
      literal_start_p -= args_p->register_end;
    }

    for (uint32_t i = const_literal_end; i < literal_end; i++)
    {
      ecma_compiled_code_t *bytecode_literal_p = ECMA_GET_INTERNAL_VALUE_POINTER (ecma_compiled_code_t,
                                                                                  literal_start_p[i]);

      /* Self references are ignored. */
      if (bytecode_literal_p != bytecode_p)
      {
        ecma_bytecode_deref (bytecode_literal_p);
      }
    }

#if ENABLED (JERRY_DEBUGGER)
    if ((JERRY_CONTEXT (debugger_flags) & JERRY_DEBUGGER_CONNECTED)
        && !(bytecode_p->status_flags & CBC_CODE_FLAGS_DEBUGGER_IGNORE)
        && jerry_debugger_send_function_cp (JERRY_DEBUGGER_RELEASE_BYTE_CODE_CP, bytecode_p))
    {
      /* Delay the byte code free until the debugger client is notified.
       * If the connection is aborted the pointer is still freed by
       * jerry_debugger_close_connection(). */
      jerry_debugger_byte_code_free_t *byte_code_free_p = (jerry_debugger_byte_code_free_t *) bytecode_p;
      jmem_cpointer_t byte_code_free_head = JERRY_CONTEXT (debugger_byte_code_free_head);

      byte_code_free_p->prev_cp = ECMA_NULL_POINTER;

      jmem_cpointer_t byte_code_free_cp;
      JMEM_CP_SET_NON_NULL_POINTER (byte_code_free_cp, byte_code_free_p);

      if (byte_code_free_head == ECMA_NULL_POINTER)
      {
        JERRY_CONTEXT (debugger_byte_code_free_tail) = byte_code_free_cp;
      }
      else
      {
        jerry_debugger_byte_code_free_t *first_byte_code_free_p;

        first_byte_code_free_p = JMEM_CP_GET_NON_NULL_POINTER (jerry_debugger_byte_code_free_t,
                                                               byte_code_free_head);
        first_byte_code_free_p->prev_cp = byte_code_free_cp;
      }

      JERRY_CONTEXT (debugger_byte_code_free_head) = byte_code_free_cp;
      return;
    }
#endif /* ENABLED (JERRY_DEBUGGER) */

#if ENABLED (JERRY_ES2015)
    if (bytecode_p->status_flags & CBC_CODE_FLAG_HAS_TAGGED_LITERALS)
    {
      ecma_length_t formal_params_number = ecma_compiled_code_get_formal_params (bytecode_p);

      uint8_t *byte_p = (uint8_t *) bytecode_p;
      byte_p += ((size_t) bytecode_p->size) << JMEM_ALIGNMENT_LOG;

      ecma_value_t *tagged_base_p = (ecma_value_t *) byte_p;
      tagged_base_p -= formal_params_number;

      ecma_collection_t *coll_p = ECMA_GET_INTERNAL_VALUE_POINTER (ecma_collection_t, tagged_base_p[-1]);

      ecma_collection_destroy (coll_p);
    }
#endif /* ENABLED (JERRY_ES2015) */

#if ENABLED (JERRY_MEM_STATS)
    jmem_stats_free_byte_code_bytes (((size_t) bytecode_p->size) << JMEM_ALIGNMENT_LOG);
#endif /* ENABLED (JERRY_MEM_STATS) */
  }
  else
  {
#if ENABLED (JERRY_BUILTIN_REGEXP)
    re_compiled_code_t *re_bytecode_p = (re_compiled_code_t *) bytecode_p;

    ecma_deref_ecma_string (ecma_get_string_from_value (re_bytecode_p->source));
#endif /* ENABLED (JERRY_BUILTIN_REGEXP) */
  }

  jmem_heap_free_block (bytecode_p,
                        ((size_t) bytecode_p->size) << JMEM_ALIGNMENT_LOG);
} /* ecma_bytecode_deref */

#if ENABLED (JERRY_ES2015)
/**
 * Get the tagged template collection of the compiled code
 *
 * @return pointer to the tagged template collection
 */
ecma_collection_t *
ecma_compiled_code_get_tagged_template_collection (const ecma_compiled_code_t *bytecode_header_p) /**< compiled code */
{
  JERRY_ASSERT (bytecode_header_p != NULL);
  JERRY_ASSERT (bytecode_header_p->status_flags & CBC_CODE_FLAG_HAS_TAGGED_LITERALS);

  uint8_t *byte_p = (uint8_t *) bytecode_header_p;
  byte_p += ((size_t) bytecode_header_p->size) << JMEM_ALIGNMENT_LOG;

  ecma_value_t *tagged_base_p = (ecma_value_t *) byte_p;
  tagged_base_p -= ecma_compiled_code_get_formal_params (bytecode_header_p);

  return ECMA_GET_INTERNAL_VALUE_POINTER (ecma_collection_t, tagged_base_p[-1]);
} /* ecma_compiled_code_get_tagged_template_collection */
#endif /* ENABLED (JERRY_ES2015) */

#if ENABLED (JERRY_LINE_INFO) || ENABLED (JERRY_ES2015_MODULE_SYSTEM) || ENABLED (JERRY_ES2015)
/**
 * Get the number of formal parameters of the compiled code
 *
 * @return number of formal parameters
 */
ecma_length_t
ecma_compiled_code_get_formal_params (const ecma_compiled_code_t *bytecode_header_p) /**< compiled code */
{
  if (!(bytecode_header_p->status_flags & CBC_CODE_FLAGS_MAPPED_ARGUMENTS_NEEDED))
  {
    return 0;
  }

  if (bytecode_header_p->status_flags & CBC_CODE_FLAGS_UINT16_ARGUMENTS)
  {
    return ((cbc_uint16_arguments_t *) bytecode_header_p)->argument_end;
  }

  return ((cbc_uint8_arguments_t *) bytecode_header_p)->argument_end;
} /* ecma_compiled_code_get_formal_params */
#endif /* ENABLED (JERRY_LINE_INFO) || ENABLED (JERRY_ES2015_MODULE_SYSTEM) || ENABLED (JERRY_ES2015) */

#if (JERRY_STACK_LIMIT != 0)
/**
 * Check the current stack usage by calculating the difference from the initial stack base.
 *
 * @return current stack usage in bytes
 */
uintptr_t JERRY_ATTR_NOINLINE
ecma_get_current_stack_usage (void)
{
  volatile int __sp;
  return (uintptr_t) (JERRY_CONTEXT (stack_base) - (uintptr_t)&__sp);
} /* ecma_get_current_stack_usage */

#endif /* (JERRY_STACK_LIMIT != 0) */

/**
 * @}
 * @}
 */
