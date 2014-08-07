#include <Eina.h>
#include "eo_lexer.h"

EAPI Eolian_Expression_Type
eolian_expression_eval(const Eolian_Expression *expr, Eolian_Expression_Mask m,
                       Eina_Value **val)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(expr, EOLIAN_EXPR_UNKNOWN);
   return database_expr_eval(expr, m, val);
}

static Eolian_Expression_Type
_eval_type(const Eolian_Expression *expr, const Eolian_Type *type,
           Eina_Value **val)
{
   if (!type)
     return EOLIAN_EXPR_UNKNOWN;
   switch (type->type)
     {
      case EOLIAN_TYPE_ALIAS:
        return _eval_type(expr, eolian_type_base_type_get(type), val);
      case EOLIAN_TYPE_POINTER:
        {
           int mask = EOLIAN_MASK_NULL;
           const Eolian_Type *base = eolian_type_base_type_get(type);
           int kw = base->name ? eo_lexer_keyword_str_to_id(base->name) : 0;
           if (kw == KW_char)
             mask |= EOLIAN_MASK_STRING;
           return database_expr_eval(expr, mask, val);
        }
      case EOLIAN_TYPE_CLASS:
        return database_expr_eval(expr, EOLIAN_MASK_NULL, val);
      case EOLIAN_TYPE_REGULAR:
        {
           int  kw = eo_lexer_keyword_str_to_id(type->name);
           if (!kw || kw < KW_byte || kw >= KW_void)
             return EOLIAN_EXPR_UNKNOWN;
           switch (kw)
             {
              case KW_byte:
              case KW_short:
              case KW_int:
              case KW_long:
              case KW_llong:
              case KW_int8:
              case KW_int16:
              case KW_int32:
              case KW_int64:
              case KW_int128:
              case KW_ssize:
              case KW_intptr:
              case KW_ptrdiff:
                return database_expr_eval(expr, EOLIAN_MASK_SINT, val);
              case KW_ubyte:
              case KW_ushort:
              case KW_uint:
              case KW_ulong:
              case KW_ullong:
              case KW_uint8:
              case KW_uint16:
              case KW_uint32:
              case KW_uint64:
              case KW_uint128:
              case KW_size:
              case KW_uintptr:
              case KW_time:
                return database_expr_eval(expr, EOLIAN_MASK_UINT, val);
              case KW_float:
              case KW_double:
              case KW_ldouble:
                return database_expr_eval(expr, EOLIAN_MASK_FLOAT, val);
              case KW_bool:
                return database_expr_eval(expr, EOLIAN_MASK_BOOL, val);
              case KW_char:
                return database_expr_eval(expr, EOLIAN_MASK_CHAR, val);
              default:
                return EOLIAN_EXPR_UNKNOWN;
             }
        }
      default:
        return EOLIAN_EXPR_UNKNOWN;
     }
}

EAPI Eolian_Expression_Type
eolian_expression_eval_type(const Eolian_Expression *expr,
                            const Eolian_Type *type,
                            Eina_Value **val)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(expr, EOLIAN_EXPR_UNKNOWN);
   return _eval_type(expr, type, val);
}

EAPI Eina_Stringshare *
eolian_expression_value_to_literal(const Eina_Value *v,
                                   Eolian_Expression_Type etp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(v, NULL);
   switch (etp)
     {
      case EOLIAN_EXPR_BOOL:
        {
           unsigned char b;
           eina_value_get(v, &b);
           return eina_stringshare_add(b ? "EINA_TRUE" : "EINA_FALSE");
        }
      case EOLIAN_EXPR_NULL:
        return eina_stringshare_add("NULL");
      case EOLIAN_EXPR_CHAR:
        {
           char c;
           eina_value_get(v, &c);
           return eina_stringshare_printf("'%c'", c);
        }
      case EOLIAN_EXPR_STRING:
        {
           char *str = eina_value_to_string(v);
           const char *ret = eina_stringshare_printf("\"%s\"", str);
           free(str);
           return ret;
        }
      case EOLIAN_EXPR_INT:
      case EOLIAN_EXPR_UINT:
      case EOLIAN_EXPR_LONG:
      case EOLIAN_EXPR_ULONG:
      case EOLIAN_EXPR_LLONG:
      case EOLIAN_EXPR_ULLONG:
      case EOLIAN_EXPR_FLOAT:
      case EOLIAN_EXPR_DOUBLE:
      case EOLIAN_EXPR_LDOUBLE:
        {
           char *str = eina_value_to_string(v);
           const char *ret = eina_stringshare_add(str);
           free(str);
           return ret;
        }
      default:
        return NULL;
     }
}
