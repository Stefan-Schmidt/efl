#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <Efl.h>

typedef struct _Efl_Animator_Data Efl_Animator_Data;
struct _Efl_Animator_Data
{
};

static void
_efl_animator_linear_timeline(Eo *obj EINA_UNUSED, Efl_Animator_Data *pd EINA_UNUSED, Eina_Promise_Owner *deadline EINA_UNUSED, double target_time EINA_UNUSED)
{
}

#include "interfaces/efl_animator.eo.c"
