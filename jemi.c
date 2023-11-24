/**
 * @file jemi.c
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
 *
 */

// *****************************************************************************
// Includes

#include "jemi.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// *****************************************************************************
// Private types and definitions

// *****************************************************************************
// Private (static) storage

jemi_node_t *s_jemi_freelist; // next available node (or null if empty)

// *****************************************************************************
// Private (static, forward) declarations

/**
 * @brief Pop one element from the freelist.  If available, set the type and
 * return it, else return NULL.
 */
static jemi_node_t *jemi_alloc(jemi_type_t type);

/**
 * @brief Print a node or a list of nodes.
 */
static jemi_node_t *emit_aux(jemi_node_t *node, jemi_writer_t writer_fn, jemi_out_buf_t *output);

/**
 * @brief Make a copy of a node and its contents, including children nodes,
 * but not siblings.
 */
static jemi_node_t *copy_node(jemi_node_t *node);

/**
 * @brief Write a string to the writer_fn, a byte at a time.
 */
static bool emit_string(jemi_writer_t writer_fn, jemi_out_buf_t *output, const char *buf);

// *****************************************************************************
// Public code

void jemi_reset(jemi_node_t *pool, size_t pool_size) {
    memset(pool, 0, pool_size * sizeof(jemi_node_t));
    // rebuild the freelist, using node->sibling as the link field
    jemi_node_t *next = NULL; // end of the linked list

    int i;
    for (i = 0; i < pool_size; i++) {
        jemi_node_t *node = &pool[i];

        node->sibling = next;
        next = node;
    }
    s_jemi_freelist = next; // reset head of the freelist
}

jemi_node_t *jemi_array(const char *key, jemi_node_t *element, ...) {
    va_list ap;
    jemi_node_t *root = jemi_alloc(JEMI_ARRAY);

    root->key = key;

    va_start(ap, element);
    root->children = element;
    while (element != NULL) {
        element->sibling = va_arg(ap, jemi_node_t *);
        element->parent = root;

        element = element->sibling;
    }
    va_end(ap);
    return root;
}
jemi_node_t *jemi_array_updateable(const char *key, ObjUpdate update, uint8_t length, jemi_node_t *element, ...) {
    va_list ap;
    jemi_node_t *root = jemi_alloc(JEMI_ARRAY);

    root->key = key;
    root->update = update;

    va_start(ap, element);
    root->children = element;
    while (element != NULL) {
        element->sibling = va_arg(ap, jemi_node_t *);
        element->parent = root;

        element = element->sibling;
    }
    va_end(ap);
    return root;
}

jemi_node_t *jemi_object(const char *key, jemi_node_t *element, ...) {
    va_list ap;
    jemi_node_t *root = jemi_alloc(JEMI_OBJECT);

    root->key = key;

    va_start(ap, element);
    root->children = element;
    while (element != NULL) {
        element->sibling = va_arg(ap, jemi_node_t *);
        element->parent = root;

        element = element->sibling;
    }
    va_end(ap);
    return root;
}

jemi_node_t *jemi_list(jemi_node_t *element, ...) {
    va_list ap;
    jemi_node_t *first = element;

    va_start(ap, element);
    while (element != NULL) {
        element->sibling = va_arg(ap, jemi_node_t *);
        element = element->sibling;
    }
    va_end(ap);
    return first;
}

jemi_node_t *jemi_integer(const char *key, int integer) {
    jemi_node_t *node = jemi_alloc(JEMI_INTEGER);
    if (node) {
        node->key = key;
        node->integer = integer;
    }
    return node;
}

jemi_node_t *jemi_string(const char *key, const char *string) {
    jemi_node_t *node = jemi_alloc(JEMI_STRING);
    if (node) {
        node->key = key;
        node->string = string;
    }
    return node;
}

jemi_node_t *jemi_bool(const char *key, bool boolean) {
    jemi_node_t *node;
    if (boolean) {
        node = jemi_true();
    } else {
        node = jemi_false();
    }
    node->key = key;
    return node;
}

jemi_node_t *jemi_true(void) { return jemi_alloc(JEMI_TRUE); }

jemi_node_t *jemi_false(void) { return jemi_alloc(JEMI_FALSE); }

jemi_node_t *jemi_null(void) { return jemi_alloc(JEMI_NULL); }

jemi_node_t *jemi_copy(jemi_node_t *root) {
    jemi_node_t *r2 = NULL;
    jemi_node_t *prev = NULL;
    jemi_node_t *node;

    while ((node = copy_node(root)) != NULL) {
        if (r2 == NULL) {
            // first time through the loop: save pointer to first element
            r2 = node;
        }
        if (prev != NULL) {
            prev->sibling = node;
        }
        prev = node;
        root = root->sibling;
    }
    return r2;
}

jemi_node_t *jemi_array_append(jemi_node_t *array, jemi_node_t *items) {
    items->parent = array;

    if (array) {
        array->children = jemi_list_append(array->children, items);
    }
    return array;
}

jemi_node_t *jemi_object_append(jemi_node_t *object, jemi_node_t *items) {
    items->parent = object;

    if (object) {
        object->children = jemi_list_append(object->children, items);
    }
    return object;
}

jemi_node_t *jemi_list_append(jemi_node_t *list, jemi_node_t *items) {
    if (list == NULL) {
        return items;
    } else {
        jemi_node_t *prev = NULL;
        jemi_node_t *node = list;
        // walk list to find last element
        while (node) {
            prev = node;
            node = node->sibling;
        }
        // prev is now null or points to last sibling in list.
        if (prev) {
            prev->sibling = items;
        }
        return list;
    }
}

jemi_node_t *jemi_float_set(jemi_node_t *node, double number) {
    if (node) {
        node->number = number;
    }
    return node;
}

jemi_node_t *jemi_integer_set(jemi_node_t *node, int64_t integer) {
    if (node) {
        node->integer = integer;
    }
    return node;
}

/**
 * @brief Update contents of a JEMI_STRING node
 *
 * NOTE: string must be null-terminated.
 */
jemi_node_t *jemi_string_set(jemi_node_t *node, const char *string) {
    if (node) {
        node->string = string;
    }
    return node;
}

/**
 * @brief Update contents of a JEMI_BOOL node
 */
jemi_node_t *jemi_bool_set(jemi_node_t *node, bool boolean) {
    if (node) {
        node->type = boolean ? JEMI_TRUE : JEMI_FALSE;
    }
    return node;
}

jemi_node_t *jemi_emit(jemi_node_t *root, jemi_writer_t writer_fn, jemi_out_buf_t *output) {

    jemi_node_t *next_node = root;

    do {
        next_node = emit_aux(next_node, writer_fn, output);

    } while (NULL != next_node && false == output->full);

    writer_fn('\0', output->buf);

    return next_node;
}

size_t jemi_available(void) {
    size_t count = 0;

    jemi_node_t *node = s_jemi_freelist;
    while (node) {
        count += 1;
        node = node->sibling;
    }
    return count;
}

// *****************************************************************************
// Private (static) code

static jemi_node_t *jemi_alloc(jemi_type_t type) {
    // pop one node from the freelist
    jemi_node_t *node = s_jemi_freelist;
    if (node) {
        s_jemi_freelist = node->sibling;
        node->sibling = NULL;
        node->type = type;
        node->state = JEMI_NODE_UNUSED;
    }
    return node;
}

static jemi_node_t *emit_aux(jemi_node_t *node, jemi_writer_t writer_fn, jemi_out_buf_t *output) {

    switch (node->type) {

        case JEMI_OBJECT: {
            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    char buf[22];
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if (!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }
            if (JEMI_KEY_USED == node->state) {
                if(!emit_string(writer_fn, output, "{"))
                    node->state = JEMI_CONTAINER_BEGUN;
            }
            if (JEMI_CONTAINER_BEGUN == node->state) {
                node->state = JEMI_CHILDREN_USED;
                return node->children;
            }
            if (JEMI_CHILDREN_USED == node->state) {
                char buf[3];
                if (node->sibling) {
                    snprintf(buf, sizeof(buf), "},");
                } else {
                    snprintf(buf, sizeof(buf), "}");
                }

                if(!emit_string(writer_fn, output, buf))
                    node->state = JEMI_NODE_USED;
            }
        } break;

        case JEMI_ARRAY: {
            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    char buf[22];
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if (!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }
            if (JEMI_KEY_USED == node->state) {
                if(!emit_string(writer_fn, output, "["))
                    node->state = JEMI_CONTAINER_BEGUN;
            }
            if (JEMI_CONTAINER_BEGUN == node->state) {
                node->state = JEMI_CHILDREN_USED;
                return node->children;
            }
            if (JEMI_CHILDREN_USED == node->state) {

                if (node->update) {
                    static int left = -1;

                    if (left) {
                        if (emit_string(writer_fn, output, ",")) return node;

                        left = node->update(node->children);

                        if (left != -1) return node->children;
                    } else {
                        left = -1;
                    }
                }

                char buf[3];
                if (node->sibling) {
                    snprintf(buf, sizeof(buf), "],");
                } else {
                    snprintf(buf, sizeof(buf), "]");
                }

                if(!emit_string(writer_fn, output, buf))
                    node->state = JEMI_NODE_USED;
            }
        } break;

        case JEMI_INTEGER: {
            char buf[22];

            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if(!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }

            if (JEMI_KEY_USED == node->state) {
                snprintf(buf, sizeof(buf), "%d", node->integer);
                if(!emit_string(writer_fn, output, buf))
                    node->state = JEMI_VAL_USED;
            }

            if (JEMI_VAL_USED == node->state) {

                if (node->sibling) {
                    if (!emit_string(writer_fn, output, ","))
                        node->state = JEMI_NODE_USED;
                } else {
                    node->state = JEMI_NODE_USED;
                }
            }
        } break;

        case JEMI_FLOAT: {
            char buf[22];

            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if(!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }

            if (JEMI_KEY_USED == node->state) {
                int i = node->number;
                if((double)i == node->number) {
                    // number can be represented as an int: supress trailing zeros
                    snprintf(buf, sizeof(buf), "%d", i);
                } else {
                    snprintf(buf, sizeof(buf), "%lf", node->number);
                }
                if(!emit_string(writer_fn, output, buf))
                    node->state = JEMI_VAL_USED;
            }

            if (JEMI_VAL_USED == node->state) {

                if (node->sibling) {
                    if (!emit_string(writer_fn, output, ","))
                        node->state = JEMI_NODE_USED;
                } else {
                    node->state = JEMI_NODE_USED;
                }
            }
        }

        case JEMI_STRING: {
            char buf[30];

            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if(!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }

            if (JEMI_KEY_USED == node->state) {
                snprintf(buf, sizeof(buf), "\"%s\"", node->string);
                if(!emit_string(writer_fn, output, buf))
                    node->state = JEMI_VAL_USED;
            }

            if (JEMI_VAL_USED == node->state) {

                if (node->sibling) {
                    if (!emit_string(writer_fn, output, ","))
                        node->state = JEMI_NODE_USED;
                } else {
                    node->state = JEMI_NODE_USED;
                }
            }
        } break;

        case JEMI_TRUE:
        case JEMI_FALSE: {
            char buf[22];

            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if(!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }

            if (JEMI_KEY_USED == node->state) {
                if(!emit_string(writer_fn, output, JEMI_TRUE == node->state ? "true" : "false"))
                    node->state = JEMI_VAL_USED;
            }

            if (JEMI_VAL_USED == node->state) {

                if (node->sibling) {
                    if (!emit_string(writer_fn, output, ","))
                        node->state = JEMI_NODE_USED;
                } else {
                    node->state = JEMI_NODE_USED;
                }
            }
        } break;

        case JEMI_NULL: {
            char buf[22];

            if (JEMI_NODE_UNUSED == node->state) {
                if (node->key) {
                    snprintf(buf, sizeof(buf), "\"%s\":", node->key);
                    if(!emit_string(writer_fn, output, buf))
                        node->state = JEMI_KEY_USED;
                } else {
                    node->state = JEMI_KEY_USED;
                }
            }

            if (JEMI_KEY_USED == node->state) {
                if(!emit_string(writer_fn, output, "null"))
                    node->state = JEMI_VAL_USED;
            }

            if (JEMI_VAL_USED == node->state) {

                if (node->sibling) {
                    if (!emit_string(writer_fn, output, ","))
                        node->state = JEMI_NODE_USED;
                } else {
                    node->state = JEMI_NODE_USED;
                }
            }
        } break;
    }

    // If node not succesfully used
    if (JEMI_NODE_USED != node->state) return node;

    // If node has a sibling, return this
    else if (node->sibling) return node->sibling;

    // If node has a parent, return this
    else if (node->parent) return node->parent;

    // No more nodes in JSON schema, return NULL
    return NULL;
}

static jemi_node_t *copy_node(jemi_node_t *node) {
    jemi_node_t *copy;
    if (node == NULL) {
        copy = NULL;
    } else {
        copy = jemi_alloc(node->type);
        switch (node->type) {
        case JEMI_ARRAY:
        case JEMI_OBJECT: {
            copy->children = jemi_copy(node->children);
        } break;
        case JEMI_STRING: {
            copy->string = node->string;
        } break;
        case JEMI_INTEGER: {
            copy->integer = node->integer;
        }
        default: {
            // no action needed
        }
        } // switch()
    }
    return copy;
}

static bool emit_string(jemi_writer_t writer_fn, jemi_out_buf_t *output, const char *data) {

    if ((strlen(output->buf) + strlen(data)) > (output->bufLen - 1)) {
        output->full = true;
        return 1;
    }

    while (*data) {
        writer_fn(*data++, output->buf);
    }
    return 0;
}

// *****************************************************************************
// End of file
