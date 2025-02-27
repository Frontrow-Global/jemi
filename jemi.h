/**
 * @file jemi.h
 *
 * MIT License
 *
 * Copyright (c) 2022 R. Dunbar Poor
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @brief a pure-C JEMI serializer for embedded systems
 */

#ifndef _JEMI_H_
#define _JEMI_H_

// *****************************************************************************
// Includes

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// *****************************************************************************
// C++ compatibility

#ifdef __cplusplus
extern "C" {
#endif

// *****************************************************************************
// Public types and definitions

#define JEMI_VERSION "1.3.0-FR"

typedef enum {
    JEMI_NULL,
    JEMI_OBJECT,
    JEMI_ARRAY,
    JEMI_INTEGER,
    JEMI_FLOAT,
    JEMI_STRING,
    JEMI_TRUE, // used as the actual boolean value
    JEMI_FALSE
} jemi_type_t;

typedef enum {
    JEMI_NODE_UNUSED,
    JEMI_CONTAINER_BEGUN,
    JEMI_KEY_USED,
    JEMI_VAL_USED,
    JEMI_CHILDREN_USED,
    JEMI_NODE_USED
} jemi_state_t; // For keeping track of written and non-written nodes


struct jemi_node_t; // Forwared declaration
typedef uint8_t (*ObjUpdate)(struct jemi_node_t *object);

typedef struct jemi_node_t {
    struct jemi_node_t *sibling; // any object may have siblings...
    struct jemi_node_t *parent;  // any object may have a parent object...
    jemi_type_t type;
    jemi_state_t state;
    const char *key;
    union {
        int integer;            // for JEMI_INTEGER
        double number;
        const char *string;          // for JEMI_STRING
        struct jemi_node_t *children; // for JEMI_ARRAY or JEMI_OBJECT
    };
    ObjUpdate update;
} jemi_node_t;


typedef struct jemi_out_buf_t {
    char *buf;
    uint16_t bufLen;
    bool full;
} jemi_out_buf_t;

/**
 * @brief Signature for the user-supplied jemi_emit function: it will be called
 * with a character and a void * pointer to a user-supplied argument.
 */
typedef void (*jemi_writer_t)(char ch, void *arg);

// *****************************************************************************
// Public declarations

/**
 * @brief Initialize the jemi system.
 *
 * Example:
 *
 *     #include "jemi.h"
 *
 *     #define JEMI_POOL_SIZE 30  // maximum number of nodes we expect to create
 *     static jemi_node_t jemi_pool[JEMI_POOL_SIZE];
 *
 *     jemi_reset(jemi_pool, JEMI_POOL_SIZE)
 *
 * @param pool User-supplied vector of jemi_node objects.
 * @param pool_size Number of jemi_node objects in the pool.
 */
void jemi_reset(jemi_node_t *pool, size_t pool_size);

// ******************************
// Creating JSON elements

/**
 * @brief Create an JSON array with zero or more sub-elements.
 *
 * NOTE: NULL must always be the last argument.  If you want to
 * create an array of zero elements (e.g. for subsequent calls to
 * `jemi_array_append()`), use the construct `jemi_array(NULL)`.
 */
jemi_node_t *jemi_array(const char *key, jemi_node_t *element, ...);

jemi_node_t *jemi_array_updateable(const char *key, ObjUpdate update, uint8_t length, jemi_node_t *element, ...);

/**
 * @brief Create a JSON object with zero or more key/value sub-elements.
 *
 * NOTE: NULL must always be the last argument.  If you want to
 * create an object of zero elements (e.g. for subsequent calls to
 * `jemi_object_append()`), use the construct `jemi_object(NULL)`.
 */
jemi_node_t *jemi_object(const char *key, jemi_node_t *element, ...);

/**
 * @brief Create a "disembodied list" of zero or more elements which are not
 * contained in an array nor in an object.  The result can be used subsequently
 * as an argument to jemi_array_append() or jemi_object_append().
 */
jemi_node_t *jemi_list(jemi_node_t *element, ...);

/**
 * @brief Create a JSON float.  This will render using %f, so it will include
 * six trailing zeroes.
 */
jemi_node_t *jemi_float(double value);

/**
 * @brief Create a JSON integer.  If your value is an integer, this will render
 * more compactly than using jemi_float().
 */
jemi_node_t *jemi_integer(const char *key, int integer);

/**
 * @brief Create a JSON string.
 *
 * NOTE: string must be null-terminated.
 */
jemi_node_t *jemi_string(const char *key, const char *string);

/**
 * @brief Create a JSON boolean (true or false).
 */
jemi_node_t *jemi_bool(const char *key, bool boolean);

/**
 * @brief Create a JSON true object.
 */
jemi_node_t *jemi_true(void);

/**
 * @brief Create a JSON false object.
 */
jemi_node_t *jemi_false(void);

/**
 * @brief Create a JSON null object.
 */
jemi_node_t *jemi_null(void);

// ******************************
// duplicating a structure

/**
 * @brief Return a copy a JSON structure.
 */
jemi_node_t *jemi_copy(jemi_node_t *root);

// ******************************
// Composing and modifying JSON elements

/**
 * @brief Add one or more items to the array body.
 */
jemi_node_t *jemi_array_append(jemi_node_t *array, jemi_node_t *items);

/**
 * @brief Add one or more items to the object body.
 */
jemi_node_t *jemi_object_append(jemi_node_t *object, jemi_node_t *items);

/**
 * @brief Add a key/value pair to the body of an object.
 *
 * The key string is wrapped in jemi_string(key).
 */
jemi_node_t *jemi_object_add_keyval(jemi_node_t *object, const char *key,
                                    jemi_node_t *value);

/**
 * @brief Add one or more items to a list.
 */
jemi_node_t *jemi_list_append(jemi_node_t *list, jemi_node_t *items);

/**
 * @brief Update contents of a JEMI_FLOAT node
 */
jemi_node_t *jemi_float_set(jemi_node_t *node, double number);

/**
 * @brief Update contents of a JEMI_INTEGER node
 */
jemi_node_t *jemi_integer_set(jemi_node_t *node, int64_t number);

/**
 * @brief Update contents of a JEMI_STRING node
 *
 * NOTE: string must be null-terminated.
 */
jemi_node_t *jemi_string_set(jemi_node_t *node, const char *string);

/**
 * @brief Update contents of a JEMI_BOOL node
 */
jemi_node_t *jemi_bool_set(jemi_node_t *node, bool boolean);

// ******************************
// Outputting JSON strings

/**
 * @brief Output a JEMI structure.
 *
 * @param root the root of the JEMI structure.
 * @param writer writer function wich char and user context args
 * @param user-supplied context
 */
jemi_node_t *jemi_emit(jemi_node_t *root, jemi_writer_t writer_fn, jemi_out_buf_t *output);

/**
 * @brief Return the number of available jemi_node objects.
 *
 * Note: jemi does its best to "silently fail" without causing a bus error.
 * This function can tell you if you've run out of available jemi_node objects.
 */
size_t jemi_available(void);

// *****************************************************************************
// End of file

#ifdef __cplusplus
}
#endif

#endif /* #ifndef _JEMI_H_ */
