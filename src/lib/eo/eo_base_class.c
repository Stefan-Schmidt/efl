#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <Eina.h>
#include <fnmatch.h>

#define EO_BASE_BETA

#include "Eo.h"
#include "eo_ptr_indirection.h"
#include "eo_private.h"

static int event_freeze_count = 0;

typedef struct _Eo_Callback_Description Eo_Callback_Description;

typedef struct
{
   const char                *id;
   const char                *comment;
} Eo_Base_Extension;

typedef struct
{
   Eina_List                 *children;
   Eo                        *parent;
   Eina_List                 *parent_list;

   Eina_Inlist               *generic_data;
   Eo                      ***wrefs;

   Eo_Base_Extension         *extension;
   Eo_Callback_Description   *callbacks;

   Eina_Inlist               *current;

   unsigned short             walking_list;
   unsigned short             event_freeze_count;
   Eina_Bool                  deletions_waiting : 1;
} Eo_Base_Data;

typedef struct
{
   EINA_INLIST;
   Eina_Stringshare          *key;
   void                      *data;
   Eina_Bool                  data_is_obj : 1;
   Eina_Bool                  data_is_value : 1;
} Eo_Generic_Data_Node;

typedef struct
{
   EINA_INLIST;
   const Eo_Event_Description *desc;
   Eo_Callback_Description *current;
} Eo_Current_Callback_Description;

static Eo_Base_Extension *
_eo_base_extension_new(void)
{
   Eo_Base_Extension *extension = calloc(1, sizeof(Eo_Base_Extension));
   return extension;
}

static void
_eo_base_extension_free(Eo_Base_Extension *extension)
{
   free(extension);
}



static void
_eo_generic_data_node_free(Eo_Generic_Data_Node *node)
{
   eina_stringshare_del(node->key);
   free(node);
}

static Eina_Bool
_eo_base_cb_key_obj_del(void *data, const Eo_Event *event)
{
   Eo *parent_obj = data;
   Eo_Base_Data *pd = eo_data_scope_get(parent_obj, EO_BASE_CLASS);

   if (pd)
     {
        Eo_Generic_Data_Node *node;

        EINA_INLIST_FOREACH(pd->generic_data, node)
          {
             if ((node->data_is_obj) && (node->data == event->obj))
               {
                  pd->generic_data = eina_inlist_remove(pd->generic_data,
                                                        EINA_INLIST_GET(node));
                  eo_event_callback_del(node->data, EO_BASE_EVENT_DEL,
                                        _eo_base_cb_key_obj_del, data);
                  _eo_generic_data_node_free(node);
                  break;
               }
          }
     }
   return EINA_TRUE;
}

static void
_eo_generic_data_del_all(Eo *obj, Eo_Base_Data *pd)
{
   Eo_Generic_Data_Node *node;

   while (pd->generic_data)
     {
        node = (Eo_Generic_Data_Node *)pd->generic_data;
        pd->generic_data = eina_inlist_remove(pd->generic_data,
                                              EINA_INLIST_GET(node));
        if (node->data_is_obj)
          {
             eo_event_callback_del(node->data, EO_BASE_EVENT_DEL,
                                   _eo_base_cb_key_obj_del, obj);
             eo_unref(node->data);
          }
        else if (node->data_is_value) eina_value_free(node->data);
        _eo_generic_data_node_free(node);
     }
}

EOLIAN static void
_eo_base_key_data_set(Eo *obj, Eo_Base_Data *pd, const char *key, const void *data)
{
   Eo_Generic_Data_Node *node;

   if (!key) return;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             if ((!node->data_is_obj) && (!node->data_is_value) &&
                 (node->data == data)) return;
             pd->generic_data = eina_inlist_remove(pd->generic_data,
                                                   EINA_INLIST_GET(node));
             if (node->data_is_obj)
               {
                  eo_event_callback_del(node->data, EO_BASE_EVENT_DEL,
                                        _eo_base_cb_key_obj_del, obj);
                  eo_unref(node->data);
               }
             else if (node->data_is_value) eina_value_free(node->data);
             _eo_generic_data_node_free(node);
             break;
          }
     }

   node = calloc(1, sizeof(Eo_Generic_Data_Node));
   if (!node) return;
   node->key = eina_stringshare_add(key);
   node->data = (void *)data;
   node->data_is_obj = EINA_FALSE;
   pd->generic_data = eina_inlist_prepend(pd->generic_data,
                                          EINA_INLIST_GET(node));
}

EOLIAN static void *
_eo_base_key_data_get(const Eo *obj, Eo_Base_Data *pd, const char *key)
{
   Eo_Generic_Data_Node *node;

   if (!key) return NULL;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             if ((!node->data_is_obj) && (!node->data_is_value))
               {
                  pd->generic_data = eina_inlist_promote(pd->generic_data,
                                                         EINA_INLIST_GET(node));
                  return node->data;
               }
             else
               {
                  ERR("Object %p key '%s' wants raw data but is not raw data",
                      obj, key);
                  return NULL;
               }
          }
     }
   return NULL;
}

EOLIAN static void
_eo_base_key_del(Eo *obj, Eo_Base_Data *pd, const char *key)
{
   Eo_Generic_Data_Node *node;

   if (!key) return;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             pd->generic_data = eina_inlist_remove(pd->generic_data,
                                                   EINA_INLIST_GET(node));
             if (node->data_is_obj)
               {
                  eo_event_callback_del(node->data, EO_BASE_EVENT_DEL,
                                        _eo_base_cb_key_obj_del, obj);
                  eo_unref(node->data);
               }
             else if (node->data_is_value) eina_value_free(node->data);
             _eo_generic_data_node_free(node);
             return;
          }
     }
}

EOLIAN static void
_eo_base_key_obj_set(Eo *obj, Eo_Base_Data *pd, const char *key, Eo *objdata)
{
   Eo_Generic_Data_Node *node;

   if (!key) return;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             if ((node->data_is_obj) && (node->data == objdata)) return;
             pd->generic_data = eina_inlist_remove(pd->generic_data,
                                                   EINA_INLIST_GET(node));
             if (node->data_is_obj)
               {
                  eo_event_callback_del(node->data, EO_BASE_EVENT_DEL,
                                        _eo_base_cb_key_obj_del, obj);
                  eo_unref(node->data);
               }
             else if (node->data_is_value) eina_value_free(node->data);
             _eo_generic_data_node_free(node);
             break;
          }
     }

   node = calloc(1, sizeof(Eo_Generic_Data_Node));
   if (!node) return;
   node->key = eina_stringshare_add(key);
   node->data = (void *)objdata;
   node->data_is_obj = EINA_TRUE;
   eo_event_callback_add(node->data, EO_BASE_EVENT_DEL,
                         _eo_base_cb_key_obj_del, obj);
   eo_ref(node->data);
   pd->generic_data = eina_inlist_prepend(pd->generic_data,
                                          EINA_INLIST_GET(node));
}

EOLIAN static Eo *
_eo_base_key_obj_get(const Eo *obj, Eo_Base_Data *pd, const char *key)
{
   Eo_Generic_Data_Node *node;

   if (!key) return NULL;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             if (node->data_is_obj)
               {
                  pd->generic_data = eina_inlist_promote(pd->generic_data,
                                                         EINA_INLIST_GET(node));
                  return node->data;
               }
             else
               {
                  ERR("Object %p key '%s' asked for object but is not an object",
                      obj, key);
                  return NULL;
               }
          }
     }
   return NULL;
}

EOLIAN static void
_eo_base_key_value_set(Eo *obj, Eo_Base_Data *pd, const char *key, Eina_Value *value)
{
   Eo_Generic_Data_Node *node;

   if (!key) return;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             if ((node->data_is_value) && (node->data == value)) return;
             pd->generic_data = eina_inlist_remove(pd->generic_data,
                                                   EINA_INLIST_GET(node));
             if (node->data_is_obj)
               {
                  eo_event_callback_del(node->data, EO_BASE_EVENT_DEL,
                                        _eo_base_cb_key_obj_del, obj);
                  eo_unref(node->data);
               }
             else if (node->data_is_value) eina_value_free(node->data);
             _eo_generic_data_node_free(node);
             break;
          }
     }

   node = calloc(1, sizeof(Eo_Generic_Data_Node));
   if (!node) return;
   node->key = eina_stringshare_add(key);
   node->data = (void *)value;
   node->data_is_value = EINA_TRUE;
   eo_event_callback_add(node->data, EO_BASE_EVENT_DEL,
                         _eo_base_cb_key_obj_del, obj);
   pd->generic_data = eina_inlist_prepend(pd->generic_data,
                                          EINA_INLIST_GET(node));
}

EOLIAN static Eina_Value *
_eo_base_key_value_get(const Eo *obj, Eo_Base_Data *pd, const char *key)
{
   Eo_Generic_Data_Node *node;

   if (!key) return NULL;
   EINA_INLIST_FOREACH(pd->generic_data, node)
     {
        if (!strcmp(node->key, key))
          {
             if (node->data_is_value)
               {
                  pd->generic_data = eina_inlist_promote(pd->generic_data,
                                                         EINA_INLIST_GET(node));
                  return node->data;
               }
             else
               {
                  ERR("Object %p key '%s' asked for value but is not a value",
                      obj, key);
                  return NULL;
               }
          }
     }
   return NULL;
}

EOLIAN static void
_eo_base_id_set(Eo *obj EINA_UNUSED, Eo_Base_Data *pd, const char *id)
{
   if ((id) && (!id[0])) id = NULL;
   if (id)
     {
        if (!pd->extension)
          pd->extension = _eo_base_extension_new();
        if (pd->extension)
          eina_stringshare_replace(&(pd->extension->id), id);
     }
   else
     {
        if (!pd->extension) return;
        if (pd->extension->id)
          {
             eina_stringshare_replace(&(pd->extension->id), id);
             if (!pd->extension->comment)
               {
                  _eo_base_extension_free(pd->extension);
                  pd->extension = NULL;
               }
          }
     }
}

EOLIAN static const char *
_eo_base_id_get(Eo *obj EINA_UNUSED, Eo_Base_Data *pd)
{
   if (!pd->extension) return NULL;
   return pd->extension->id;
}

static inline Eina_Bool
_idmatch(const char *match, Eina_Bool is_glob, const char *str)
{
   if (str)
     {
        if (is_glob)
          {
             // if match string is empty - then it matches - same as "*"
             if (!match[0]) return EINA_TRUE;
             // if match string is "*" special case it and match
             if ((match[0] == '*') && (match[1] == 0)) return EINA_TRUE;
             // actual compare
             if (!fnmatch(match, str, 0)) return EINA_TRUE;
          }
        else
          {
             // if match string is empty - then it matches - same as "*"
             if (!match[0]) return EINA_TRUE;
             // if pointers are the same they must be the same
             if (match == str) return EINA_TRUE;
             // actual compare
             if (!strcmp(match, str)) return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}

static inline Eina_Bool
_matchall(const char *match)
{
   if ((match[0] == 0) || ((match[0] == '*') && (match[1] == 0)))
     return EINA_TRUE;
   return EINA_FALSE;
}

static Eina_Bool
_hasglob(const char *match)
{
   if (strpbrk(match, "*?[")) return EINA_TRUE;
   return EINA_FALSE;
}

static Eina_Bool
_ismultiglob(const char *match)
{
   if ((match[0] == '*') && (match[1] == '*') && (match[2] == 0))
     return EINA_TRUE;
   if ((match[0] == '*') && (match[1] == '*') && (match[2] == '/'))
     return EINA_TRUE;
   if ((match[0] == '/') && (match[1] == '*') && (match[2] == '*') && (match[3] == 0))
     return EINA_TRUE;
   if ((match[0] == '/') && (match[1] == '*') && (match[2] == '*') && (match[3] == '/'))
     return EINA_TRUE;
   return EINA_FALSE;
}

EOLIAN static Eo_Base *
_eo_base_id_find(Eo *obj EINA_UNUSED, Eo_Base_Data *pd, const char *search)
{
   Eina_List *l;
   Eo *child;
   const char *id, *p, *klass_name;
   size_t len;

   // notes:
   // if search contains NO "/" char, then its just a name search.
   // if there is one or more "/" chars, then these are explicitly object
   // delimiters.
   // a name of "**" means 0 or more objects in the heirachy chain
   // if the string has no "/" char at the start, it implies "/**/"
   // a name can be a name or the form "class:name" where the object must
   // be of class named "class" and name "name". if "name" is empty like:
   // "class:" then an object of any name will match like "class:*". an
   // empty class like ":name" is the sanme as "*:name" which is the same
   // as "name". class ane name of course can be basic globs but not **

   // search string NULL or "" is invalid
   if (!search) return NULL;
   if (!search[0]) return NULL;

   len = strlen(search);

   if (strchr(search, '/'))
     {
        ERR("Looking up object by path '%s' is not supported", search);
        return NULL;
     }
   else
     {
        // if this is a multi glob - "**" then we don't have a name or
        // class to match at all so just don't look
        if (_ismultiglob(search)) return NULL;
        // check if this is "class:name" or just "name"
        if ((p = strchr(search, ':')))
          {
             // "class:name"
             char *klass = alloca(len);
             char *name = alloca(len);
             Eina_Bool klass_glob = EINA_FALSE;
             Eina_Bool name_glob = EINA_FALSE;

             // split class:name into 2 strings dropping :
             strncpy(klass, search, p - search);
             klass[p - search] = 0;
             strcpy(name, p + 1);

             // figure out if class or name are globs
             klass_glob = _hasglob(klass);
             name_glob = _hasglob(name);
             EINA_LIST_FOREACH(pd->children, l, child)
               {
                  id = eo_id_get(child);
                  klass_name = eo_class_name_get(eo_class_get(child));
                  if (_idmatch(klass, klass_glob, klass_name) &&
                      (((!_matchall(klass)) && (!id) && (_matchall(name))) ||
                       ((id) && _idmatch(name, name_glob, id))))
                    return child;
                  child = eo_id_find(child, search);
                  if (child) return child;
               }
          }
        else
          {
             if (_hasglob(search))
               {
                  // we have a glob - fnmatch
                  EINA_LIST_FOREACH(pd->children, l, child)
                    {
                       id = eo_id_get(child);
                       if ((id) && (_idmatch(search, EINA_TRUE, id)))
                         return child;
                       child = eo_id_find(child, search);
                       if (child) return child;
                    }
               }
             else
               {
                  // fast path for simple "name"
                  EINA_LIST_FOREACH(pd->children, l, child)
                    {
                       id = eo_id_get(child);
                       if ((id) && (_idmatch(search, EINA_FALSE, id)))
                         return child;
                       child = eo_id_find(child, search);
                       if (child) return child;
                    }
               }
          }
     }
   return NULL;
}

EOLIAN static void
_eo_base_comment_set(Eo *obj EINA_UNUSED, Eo_Base_Data *pd, const char *comment)
{
   if ((comment) && (!comment[0])) comment = NULL;
   if (comment)
     {
        if (!pd->extension)
          pd->extension = _eo_base_extension_new();
        if (pd->extension)
          eina_stringshare_replace(&(pd->extension->comment), comment);
     }
   else
     {
        if (!pd->extension) return;
        if (pd->extension->comment)
          {
             eina_stringshare_replace(&(pd->extension->comment), comment);
             if (!pd->extension->id)
               {
                  _eo_base_extension_free(pd->extension);
                  pd->extension = NULL;
               }
          }
     }
}

EOLIAN static const char *
_eo_base_comment_get(Eo *obj EINA_UNUSED, Eo_Base_Data *pd)
{
   if (!pd->extension) return NULL;
   return pd->extension->comment;
}


EOLIAN static void
_eo_base_parent_set(Eo *obj, Eo_Base_Data *pd, Eo *parent_id)
{
   if (pd->parent == parent_id)
     return;

   if (eo_composite_part_is(obj) && pd->parent)
     {
        eo_composite_detach(pd->parent, obj);
     }

   if (pd->parent)
     {
        Eo_Base_Data *old_parent_pd;

        old_parent_pd = eo_data_scope_get(pd->parent, EO_BASE_CLASS);
        if (old_parent_pd)
          {
             old_parent_pd->children = eina_list_remove_list(old_parent_pd->children,
                                                             pd->parent_list);
             pd->parent_list = NULL;
          }
        else
          {
             ERR("CONTACT DEVS!!! SHOULD NEVER HAPPEN!!! Old parent %p for object %p is not a valid Eo object.",
                 pd->parent, obj);
          }

        /* Only unref if we don't have a new parent instead. */
        if (!parent_id)
          {
             eo_unref(obj);
          }
     }

   /* Set new parent */
   if (parent_id)
     {
        Eo_Base_Data *parent_pd = NULL;
        parent_pd = eo_data_scope_get(parent_id, EO_BASE_CLASS);

        if (EINA_LIKELY(parent_pd != NULL))
          {
             pd->parent = parent_id;
             parent_pd->children = eina_list_append(parent_pd->children, obj);
             pd->parent_list = eina_list_last(parent_pd->children);
          }
        else
          {
             pd->parent = NULL;
             ERR("New parent %p for object %p is not a valid Eo object.",
                 parent_id, obj);
          }
     }
   else
     {
        pd->parent = NULL;
     }
}

EOLIAN static Eo *
_eo_base_parent_get(Eo *obj EINA_UNUSED, Eo_Base_Data *pd)
{
   return pd->parent;
}

EOLIAN static Eina_Bool
_eo_base_finalized_get(Eo *obj_id, Eo_Base_Data *pd EINA_UNUSED)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);

   return obj->finalized;
}

// XXX: this should be Efl_Loop *;
EOLIAN static Eo_Base *
_eo_base_loop_get(Eo *obj EINA_UNUSED, Eo_Base_Data *pd)
{
   if (!pd->parent) return eo_loop_get(pd->parent);
   return NULL;
}


EOLIAN static Eo_Base *
_eo_base_interface_get(Eo *obj, Eo_Base_Data *pd, Eo_Base *interface)
{
   if (pd->parent) return eo_interface_get(pd->parent, interface);
   return NULL;
}


/* Children accessor */
typedef struct _Eo_Children_Iterator Eo_Children_Iterator;
struct _Eo_Children_Iterator
{
   Eina_Iterator iterator;
   Eina_List *current;
   _Eo_Object *obj;
   Eo *obj_id;
};

static Eina_Bool
_eo_children_iterator_next(Eo_Children_Iterator *it, void **data)
{
   if (!it->current) return EINA_FALSE;

   if (data) *data = eina_list_data_get(it->current);
   it->current = eina_list_next(it->current);

   return EINA_TRUE;
}

static Eo *
_eo_children_iterator_container(Eo_Children_Iterator *it)
{
   return it->obj_id;
}

static void
_eo_children_iterator_free(Eo_Children_Iterator *it)
{
   _Eo_Class *klass;
   _Eo_Object *obj;

   klass = (_Eo_Class*) it->obj->klass;
   obj = it->obj;

   eina_spinlock_take(&klass->iterators.trash_lock);
   if (klass->iterators.trash_count < 8)
     {
        klass->iterators.trash_count++;
        eina_trash_push(&klass->iterators.trash, it);
     }
   else
     {
        free(it);
     }
   eina_spinlock_release(&klass->iterators.trash_lock);

   _eo_unref(obj);
}

EOLIAN static Eina_Iterator *
_eo_base_children_iterator_new(Eo *obj_id, Eo_Base_Data *pd)
{
   _Eo_Class *klass;
   Eo_Children_Iterator *it;

   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, NULL);

   if (!pd->children) return NULL;

   klass = (_Eo_Class *) obj->klass;

   eina_spinlock_take(&klass->iterators.trash_lock);
   it = eina_trash_pop(&klass->iterators.trash);
   if (it)
     {
        klass->iterators.trash_count--;
        memset(it, 0, sizeof (Eo_Children_Iterator));
     }
   else
     {
        it = calloc(1, sizeof (Eo_Children_Iterator));
     }
   eina_spinlock_release(&klass->iterators.trash_lock);
   if (!it) return NULL;

   EINA_MAGIC_SET(&it->iterator, EINA_MAGIC_ITERATOR);
   it->current = pd->children;
   it->obj = _eo_ref(obj);
   it->obj_id = obj_id;

   it->iterator.next = FUNC_ITERATOR_NEXT(_eo_children_iterator_next);
   it->iterator.get_container = FUNC_ITERATOR_GET_CONTAINER(_eo_children_iterator_container);
   it->iterator.free = FUNC_ITERATOR_FREE(_eo_children_iterator_free);

   return (Eina_Iterator *)it;
}

EOLIAN static void
_eo_base_dbg_info_get(Eo *obj EINA_UNUSED, Eo_Base_Data *pd EINA_UNUSED, Eo_Dbg_Info *root_node EINA_UNUSED)
{  /* No info required in the meantime */
   return;
}

/* Weak reference. */

static inline size_t
_wref_count(Eo_Base_Data *pd)
{
   size_t count = 0;
   if (!pd->wrefs)
      return 0;

   Eo ***itr;
   for (itr = pd->wrefs; *itr; itr++)
      count++;

   return count;
}

EOLIAN static void
_eo_base_wref_add(Eo *obj, Eo_Base_Data *pd, Eo **wref)
{
   size_t count;
   Eo ***tmp;

   count = _wref_count(pd);
   count += 1; /* New wref. */

   tmp = realloc(pd->wrefs, sizeof(*pd->wrefs) * (count + 1));
   if (!tmp) return;
   pd->wrefs = tmp;

   pd->wrefs[count - 1] = wref;
   pd->wrefs[count] = NULL;
   *wref = obj;
}

EOLIAN static void
_eo_base_wref_del(Eo *obj, Eo_Base_Data *pd, Eo **wref)
{
   size_t count;

   if (*wref != obj)
     {
        ERR("Wref is a weak ref to %p, while this function was called on %p.",
              *wref, obj);
        return;
     }

   if (!pd->wrefs)
     {
        ERR("There are no weak refs for object %p", obj);
        *wref = NULL;
        return;
     }

   /* Move the last item in the array instead of the current wref. */
   count = _wref_count(pd);

     {
        Eo ***itr;
        for (itr = pd->wrefs; *itr; itr++)
          {
             if (*itr == wref)
               {
                  *itr = pd->wrefs[count - 1];
                  break;
               }
          }

        if (!*itr)
          {
             ERR("Wref %p is not associated with object %p", wref, obj);
             *wref = NULL;
             return;
          }
     }

   if (count > 1)
     {
        Eo ***tmp;
        // No count--; because of the NULL that is not included in the count. */
        tmp = realloc(pd->wrefs, sizeof(*pd->wrefs) * count);
        if (!tmp) return;
        pd->wrefs = tmp;
        pd->wrefs[count - 1] = NULL;
     }
   else
     {
        free(pd->wrefs);
        pd->wrefs = NULL;
     }

   *wref = NULL;
}

static inline void
_wref_destruct(Eo_Base_Data *pd)
{
   Eo ***itr;
   if (!pd->wrefs)
      return;

   for (itr = pd->wrefs; *itr; itr++)
     {
        **itr = NULL;
     }

   free(pd->wrefs);
}

/* EOF Weak reference. */

/* Event callbacks */

/* Callbacks */

/* XXX: Legacy support, remove when legacy is dead. */
static Eina_Hash *_legacy_events_hash = NULL;

EAPI const Eo_Event_Description *
eo_base_legacy_only_event_description_get(const char *_event_name)
{
   Eina_Stringshare *event_name = eina_stringshare_add(_event_name);
   Eo_Event_Description *event_desc = eina_hash_find(_legacy_events_hash, event_name);
   if (!event_desc)
     {
        event_desc = calloc(1, sizeof(Eo_Event_Description));
        event_desc->name = event_name;
        event_desc->legacy_is = EINA_TRUE;
        eina_hash_add(_legacy_events_hash, event_name, event_desc);
     }
   else
     {
        eina_stringshare_del(event_name);
     }

   return event_desc;
}

static inline Eina_Bool
_legacy_event_desc_is(const Eo_Event_Description *desc)
{
   return desc->legacy_is;
}

static void
_legacy_events_hash_free_cb(void *_desc)
{
   Eo_Event_Description *desc = _desc;
   eina_stringshare_del(desc->name);
   free(desc);
}

/* EOF Legacy */

struct _Eo_Callback_Description
{
   Eo_Callback_Description *next;

   union
     {
        Eo_Callback_Array_Item item;
        const Eo_Callback_Array_Item *item_array;
     } items;

   void *func_data;
   Eo_Callback_Priority priority;

   Eina_Bool delete_me : 1;
   Eina_Bool func_array : 1;
};

/* Actually remove, doesn't care about walking list, or delete_me */
static void
_eo_callback_remove(Eo_Base_Data *pd, Eo_Callback_Description *cb)
{
   Eo_Callback_Description *itr, *pitr = NULL;

   itr = pd->callbacks;

   for ( ; itr; )
     {
        Eo_Callback_Description *titr = itr;
        itr = itr->next;

        if (titr == cb)
          {
             if (pitr)
               {
                  pitr->next = titr->next;
               }
             else
               {
                  pd->callbacks = titr->next;
               }
             free(titr);
          }
        else
          {
             pitr = titr;
          }
     }
}

/* Actually remove, doesn't care about walking list, or delete_me */
static void
_eo_callback_remove_all(Eo_Base_Data *pd)
{
   while (pd->callbacks)
     {
        Eo_Callback_Description *next = pd->callbacks->next;
        free(pd->callbacks);
        pd->callbacks = next;
     }
}

static void
_eo_callbacks_clear(Eo_Base_Data *pd)
{
   Eo_Callback_Description *cb = NULL;

   /* If there are no deletions waiting. */
   if (!pd->deletions_waiting)
      return;

   /* Abort if we are currently walking the list. */
   if (pd->walking_list > 0)
      return;

   pd->deletions_waiting = EINA_FALSE;

   for (cb = pd->callbacks; cb; )
     {
        Eo_Callback_Description *titr = cb;
        cb = cb->next;

        if (titr->delete_me)
          {
             _eo_callback_remove(pd, titr);
          }
     }
}

static void
_eo_callbacks_sorted_insert(Eo_Base_Data *pd, Eo_Callback_Description *cb)
{
   Eo_Callback_Description *itr, *itrp = NULL;
   for (itr = pd->callbacks; itr && (itr->priority < cb->priority);
         itr = itr->next)
     {
        itrp = itr;
     }

   if (itrp)
     {
        cb->next = itrp->next;
        itrp->next = cb;
     }
   else
     {
        cb->next = pd->callbacks;
        pd->callbacks = cb;
     }
}

EOLIAN static Eina_Bool
_eo_base_event_callback_priority_add(Eo *obj, Eo_Base_Data *pd,
                    const Eo_Event_Description *desc,
                    Eo_Callback_Priority priority,
                    Eo_Event_Cb func,
                    const void *user_data)
{
   const Eo_Callback_Array_Item arr[] = { {desc, func}, {NULL, NULL}};
   Eo_Callback_Description *cb;

   cb = calloc(1, sizeof(*cb));
   if (!cb || !desc || !func)
     {
        ERR("Tried adding callback with invalid values: cb: %p desc: %p func: %p\n", cb, desc, func);
        free(cb);
        return EINA_FALSE;
     }
   cb->items.item.desc = desc;
   cb->items.item.func = func;
   cb->func_data = (void *) user_data;
   cb->priority = priority;
   _eo_callbacks_sorted_insert(pd, cb);

   eo_event_callback_call(obj, EO_BASE_EVENT_CALLBACK_ADD, (void *)arr);

   return EINA_TRUE;
}

EOLIAN static Eina_Bool
_eo_base_event_callback_del(Eo *obj, Eo_Base_Data *pd,
                    const Eo_Event_Description *desc,
                    Eo_Event_Cb func,
                    const void *user_data)
{
   Eo_Callback_Description *cb;

   for (cb = pd->callbacks; cb; cb = cb->next)
     {
        if (!cb->delete_me && (cb->items.item.desc == desc) &&
              (cb->items.item.func == func) && (cb->func_data == user_data))
          {
             const Eo_Callback_Array_Item arr[] = { {desc, func}, {NULL, NULL}};

             cb->delete_me = EINA_TRUE;
             pd->deletions_waiting = EINA_TRUE;
             _eo_callbacks_clear(pd);
             eo_event_callback_call(obj, EO_BASE_EVENT_CALLBACK_DEL, (void *)arr);
             return EINA_TRUE;
          }
     }

   DBG("Callback of object %p with function %p and data %p not found.", obj, func, user_data);
   return EINA_FALSE;
}

EOLIAN static Eina_Bool
_eo_base_event_callback_array_priority_add(Eo *obj, Eo_Base_Data *pd,
                          const Eo_Callback_Array_Item *array,
                          Eo_Callback_Priority priority,
                          const void *user_data)
{
   Eo_Callback_Description *cb;

   cb = calloc(1, sizeof(*cb));
   if (!cb || !array)
     {
        ERR("Tried adding array of callbacks with invalid values: cb: %p array: %p\n", cb, array);
        free(cb);
        return EINA_FALSE;
     }
   cb->func_data = (void *) user_data;
   cb->priority = priority;
   cb->items.item_array = array;
   cb->func_array = EINA_TRUE;
   _eo_callbacks_sorted_insert(pd, cb);

   eo_event_callback_call(obj, EO_BASE_EVENT_CALLBACK_ADD, (void *)array);

   return EINA_TRUE;
}

EOLIAN static Eina_Bool
_eo_base_event_callback_array_del(Eo *obj, Eo_Base_Data *pd,
                 const Eo_Callback_Array_Item *array,
                 const void *user_data)
{
   Eo_Callback_Description *cb;

   for (cb = pd->callbacks; cb; cb = cb->next)
     {
        if (!cb->delete_me &&
              (cb->items.item_array == array) && (cb->func_data == user_data))
          {
             cb->delete_me = EINA_TRUE;
             pd->deletions_waiting = EINA_TRUE;
             _eo_callbacks_clear(pd);

             eo_event_callback_call(obj, EO_BASE_EVENT_CALLBACK_DEL, (void *)array);
             return EINA_TRUE;
          }
     }

   DBG("Callback of object %p with function array %p and data %p not found.", obj, array, user_data);
   return EINA_FALSE;
}

static Eina_Bool
_cb_desc_match(const Eo_Event_Description *a, const Eo_Event_Description *b)
{
   /* If one is legacy and the other is not, strcmp. Else, pointer compare. */
   if (EINA_UNLIKELY(_legacy_event_desc_is(a) != _legacy_event_desc_is(b)))
     {
        return !strcmp(a->name, b->name);
     }

   return (a == b);
}

EOLIAN static Eina_Bool
_eo_base_event_callback_call(Eo *obj_id, Eo_Base_Data *pd,
            const Eo_Event_Description *desc,
            void *event_info)
{
   Eina_Bool ret = EINA_TRUE;
   Eo_Callback_Description *cb;
   Eo_Current_Callback_Description *lookup = NULL;
   Eo_Current_Callback_Description saved;
   Eo_Event ev;

   ev.obj = obj_id;
   ev.desc = desc;
   ev.info = event_info;

   pd->walking_list++;

   // Handle event that require to restart where we were in the nested list walking
   if (desc->restart)
     {
        EINA_INLIST_FOREACH(pd->current, lookup)
          if (lookup->desc == desc)
            break;

        // This is the first event to trigger it, so register it here
        if (!lookup)
          {
             // This following trick get us a zero allocation list
             saved.desc = desc;
             saved.current = NULL;
             lookup = &saved;
             // Ideally there will most of the time be only one item in this list
             // But just to speed up things, prepend so we find it fast at the end
             // of this function
             pd->current = eina_inlist_prepend(pd->current, EINA_INLIST_GET(lookup));
          }

        if (!lookup->current) lookup->current = pd->callbacks;
        cb = lookup->current;
     }
   else
     {
        cb = pd->callbacks;
     }

   for (; cb; cb = cb->next)
     {
        if (!cb->delete_me)
          {
             if (cb->func_array)
               {
                  const Eo_Callback_Array_Item *it;

                  for (it = cb->items.item_array; it->func; it++)
                    {
                       if (!_cb_desc_match(it->desc, desc))
                          continue;
                       if (!it->desc->unfreezable &&
                           (event_freeze_count || pd->event_freeze_count))
                          continue;

                       // Handle nested restart of walking list
                       if (lookup) lookup->current = cb->next;
                       /* Abort callback calling if the func says so. */
                       if (!it->func((void *) cb->func_data, &ev))
                         {
                            ret = EINA_FALSE;
                            goto end;
                         }
                       // We have actually walked this list during a nested call
                       if (lookup &&
                           lookup->current == NULL)
                         goto end;
                    }
               }
             else
               {
                  if (!_cb_desc_match(cb->items.item.desc, desc))
                    continue;
                  if (!cb->items.item.desc->unfreezable &&
                      (event_freeze_count || pd->event_freeze_count))
                    continue;

                  // Handle nested restart of walking list
                  if (lookup) lookup->current = cb->next;
                  /* Abort callback calling if the func says so. */
                  if (!cb->items.item.func((void *) cb->func_data, &ev))
                    {
                       ret = EINA_FALSE;
                       goto end;
                    }
                  // We have actually walked this list during a nested call
                  if (lookup &&
                      lookup->current == NULL)
                    goto end;
               }
          }
     }

end:
   // Handling restarting list walking complete exit.
   if (lookup) lookup->current = NULL;
   if (lookup == &saved)
     {
        pd->current = eina_inlist_remove(pd->current, EINA_INLIST_GET(lookup));
     }

   pd->walking_list--;
   _eo_callbacks_clear(pd);

   return ret;
}

static Eina_Bool
_eo_event_forwarder_callback(void *data, const Eo_Event *event)
{
   Eo *new_obj = (Eo *) data;
   Eina_Bool ret = EINA_FALSE;

   ret = eo_event_callback_call(new_obj, event->desc, event->info);

   return ret;
}

/* FIXME: Change default priority? Maybe call later? */
EOLIAN static void
_eo_base_event_callback_forwarder_add(Eo *obj, Eo_Base_Data *pd EINA_UNUSED,
                     const Eo_Event_Description *desc,
                     Eo *new_obj)
{

   /* FIXME: Add it EO_MAGIC_RETURN(new_obj, EO_EINA_MAGIC); */

   eo_event_callback_add(obj, desc, _eo_event_forwarder_callback, new_obj);
}

EOLIAN static void
_eo_base_event_callback_forwarder_del(Eo *obj, Eo_Base_Data *pd EINA_UNUSED,
                     const Eo_Event_Description *desc,
                     Eo *new_obj)
{

   /* FIXME: Add it EO_MAGIC_RETURN(new_obj, EO_EINA_MAGIC); */

   eo_event_callback_del(obj, desc, _eo_event_forwarder_callback, new_obj);
}

EOLIAN static void
_eo_base_event_freeze(Eo *obj EINA_UNUSED, Eo_Base_Data *pd)
{
   pd->event_freeze_count++;
}

EOLIAN static void
_eo_base_event_thaw(Eo *obj, Eo_Base_Data *pd)
{
   if (pd->event_freeze_count > 0)
     {
        pd->event_freeze_count--;
     }
   else
     {
        ERR("Events for object %p have already been thawed.", obj);
     }
}

EOLIAN static int
_eo_base_event_freeze_count_get(Eo *obj EINA_UNUSED, Eo_Base_Data *pd)
{
   return pd->event_freeze_count;
}

EOLIAN static void
_eo_base_event_global_freeze(Eo *klass EINA_UNUSED, void *pd EINA_UNUSED)
{
   event_freeze_count++;
}

EOLIAN static void
_eo_base_event_global_thaw(Eo *klass EINA_UNUSED, void *pd EINA_UNUSED)
{
   if (event_freeze_count > 0)
     {
        event_freeze_count--;
     }
   else
     {
        ERR("Global events have already been thawed.");
     }
}

EOLIAN static int
_eo_base_event_global_freeze_count_get(Eo *klass EINA_UNUSED, void *pd EINA_UNUSED)
{
   return event_freeze_count;
}

EOLIAN static Eina_Bool
_eo_base_composite_attach(Eo *parent_id, Eo_Base_Data *pd EINA_UNUSED, Eo *comp_obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(comp_obj_id, comp_obj, EINA_FALSE);
   EO_OBJ_POINTER_RETURN_VAL(parent_id, parent, EINA_FALSE);

   if (!eo_isa(parent_id, _eo_class_id_get(comp_obj->klass))) return EINA_FALSE;

     {
        Eina_List *itr;
        Eo *emb_obj_id;
        EINA_LIST_FOREACH(parent->composite_objects, itr, emb_obj_id)
          {
             EO_OBJ_POINTER_RETURN_VAL(emb_obj_id, emb_obj, EINA_FALSE);
             if(emb_obj->klass == comp_obj->klass)
               return EINA_FALSE;
          }
     }

   comp_obj->composite = EINA_TRUE;
   parent->composite_objects = eina_list_prepend(parent->composite_objects, comp_obj_id);

   eo_parent_set(comp_obj_id, parent_id);

   return EINA_TRUE;
}

EOLIAN static Eina_Bool
_eo_base_composite_detach(Eo *parent_id, Eo_Base_Data *pd EINA_UNUSED, Eo *comp_obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(comp_obj_id, comp_obj, EINA_FALSE);
   EO_OBJ_POINTER_RETURN_VAL(parent_id, parent, EINA_FALSE);

   if (!comp_obj->composite)
      return EINA_FALSE;

   comp_obj->composite = EINA_FALSE;
   parent->composite_objects = eina_list_remove(parent->composite_objects, comp_obj_id);
   eo_parent_set(comp_obj_id, NULL);

   return EINA_TRUE;
}

EOLIAN static Eina_Bool
_eo_base_composite_part_is(Eo *comp_obj_id, Eo_Base_Data *pd EINA_UNUSED)
{
   EO_OBJ_POINTER_RETURN_VAL(comp_obj_id, comp_obj, EINA_FALSE);

   return comp_obj->composite;
}

/* Eo_Dbg */
EAPI void
eo_dbg_info_free(Eo_Dbg_Info *info)
{
   eina_value_flush(&(info->value));
   free(info);
}

static Eina_Bool
_eo_dbg_info_setup(const Eina_Value_Type *type, void *mem)
{
   memset(mem, 0, type->value_size);
   return EINA_TRUE;
}

static Eina_Bool
_eo_dbg_info_flush(const Eina_Value_Type *type EINA_UNUSED, void *_mem)
{
   Eo_Dbg_Info *mem = *(Eo_Dbg_Info **) _mem;
   eina_stringshare_del(mem->name);
   eina_value_flush(&(mem->value));
   free(mem);
   return EINA_TRUE;
}

static Eina_Bool
_eo_dbg_info_copy(const Eina_Value_Type *type EINA_UNUSED, const void *_src, void *_dst)
{
   const Eo_Dbg_Info **src = (const Eo_Dbg_Info **) _src;
   Eo_Dbg_Info **dst = _dst;

   *dst = calloc(1, sizeof(Eo_Dbg_Info));
   if (!*dst) return EINA_FALSE;
   (*dst)->name = eina_stringshare_ref((*src)->name);
   eina_value_copy(&((*src)->value), &((*dst)->value));
   return EINA_TRUE;
}

static Eina_Bool
_eo_dbg_info_convert_to(const Eina_Value_Type *type EINA_UNUSED, const Eina_Value_Type *convert, const void *type_mem, void *convert_mem)
{
   /* FIXME: For the meanwhile, just use the inner type for the value. */
   const Eo_Dbg_Info **src = (const Eo_Dbg_Info **) type_mem;
   if (convert == EINA_VALUE_TYPE_STRINGSHARE ||
       convert == EINA_VALUE_TYPE_STRING)
     {
        Eina_Bool ret;
        const char *other_mem;
        char *inner_val = eina_value_to_string(&(*src)->value);
        other_mem = inner_val;
        ret = eina_value_type_pset(convert, convert_mem, &other_mem);
        free(inner_val);
        return ret;
     }

   eina_error_set(EINA_ERROR_VALUE_FAILED);
   return EINA_FALSE;
}

static Eina_Bool
_eo_dbg_info_pset(const Eina_Value_Type *type EINA_UNUSED, void *_mem, const void *_ptr)
{
   Eo_Dbg_Info **mem = _mem;
   if (*mem)
     free(*mem);
   *mem = (void *) _ptr;
   return EINA_TRUE;
}

static Eina_Bool
_eo_dbg_info_pget(const Eina_Value_Type *type EINA_UNUSED, const void *_mem, void *_ptr)
{
   Eo_Dbg_Info **ptr = _ptr;
   *ptr = (void *) _mem;
   return EINA_TRUE;
}

static const Eina_Value_Type _EO_DBG_INFO_TYPE = {
   EINA_VALUE_TYPE_VERSION,
   sizeof(Eo_Dbg_Info *),
   "Eo_Dbg_Info_Ptr",
   _eo_dbg_info_setup,
   _eo_dbg_info_flush,
   _eo_dbg_info_copy,
   NULL,
   _eo_dbg_info_convert_to,
   NULL,
   NULL,
   _eo_dbg_info_pset,
   _eo_dbg_info_pget
};

EAPI const Eina_Value_Type *EO_DBG_INFO_TYPE = &_EO_DBG_INFO_TYPE;


/* EOF event callbacks */

/* EO_BASE_CLASS stuff */
#define MY_CLASS EO_BASE_CLASS

EOLIAN static Eo *
_eo_base_constructor(Eo *obj, Eo_Base_Data *pd EINA_UNUSED)
{
   DBG("%p - %s.", obj, eo_class_name_get(obj));

   _eo_condtor_done(obj);

   return obj;
}

EOLIAN static void
_eo_base_destructor(Eo *obj, Eo_Base_Data *pd)
{
   Eo *child;

   DBG("%p - %s.", obj, eo_class_name_get(obj));

   // special removal - remove from children list by hand after getting
   // child handle in case unparent method is overridden and does
   // extra things like removes other children too later on in the list
   while (pd->children)
     {
        child = eina_list_data_get(pd->children);
        eo_parent_set(child, NULL);
     }

   if (pd->parent)
     {
        ERR("Object '%p' still has a parent at the time of destruction.", obj);
        eo_ref(obj);
        eo_parent_set(obj, NULL);
     }

   _eo_generic_data_del_all(obj, pd);
   _wref_destruct(pd);
   _eo_callback_remove_all(pd);

   if (pd->extension)
     {
        eina_stringshare_del(pd->extension->id);
        pd->extension->id = NULL;
        eina_stringshare_del(pd->extension->comment);
        pd->extension->comment = NULL;
        _eo_base_extension_free(pd->extension);
        pd->extension = NULL;
     }

   _eo_condtor_done(obj);
}

EOLIAN static Eo *
_eo_base_finalize(Eo *obj, Eo_Base_Data *pd EINA_UNUSED)
{
   return obj;
}

EOLIAN static void
_eo_base_class_constructor(Eo_Class *klass EINA_UNUSED)
{
   event_freeze_count = 0;
   _legacy_events_hash = eina_hash_stringshared_new(_legacy_events_hash_free_cb);
}

EOLIAN static void
_eo_base_class_destructor(Eo_Class *klass EINA_UNUSED)
{
   eina_hash_free(_legacy_events_hash);
}

#include "eo_base.eo.c"
