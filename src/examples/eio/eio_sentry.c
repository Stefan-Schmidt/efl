#if HAVE_CONFIG_H
#include <config.h>
#endif


#include <stdlib.h>
#include <stdio.h>

#include <Eina.h>
#include <Eio.h>
#include <Ecore.h>

Eina_Bool
sentry_cb(void *data, const Eo_Event *event)
{
    Eio_Sentry_Event *event_info = event->info;

    printf("Event on monitored path %s", event_info->source);
    printf("Created file %s\n", event_info->trigger);

    ecore_main_loop_quit();

    return EINA_FALSE;
}

void
monitor_stuff(void *data)
{
    const char *path = data;
    Eio_Sentry *sentry = eo_add(EIO_SENTRY_CLASS, NULL);
    eo_event_callback_add(sentry, EIO_SENTRY_EVENT_FILE_CREATED, (Eo_Event_Cb)&sentry_cb, NULL);

    printf("Starting monitoring path %s\n", path);
    eio_sentry_add(sentry, path);
}

int
main(int argc, char const *argv[])
{
    eio_init();
    ecore_init();

    const char *path = getenv("HOME");

    if (argc > 1)
        path = argv[1];

    Ecore_Job *job = ecore_job_add(&monitor_stuff, path);

    ecore_main_loop_begin();

    ecore_shutdown();
    eio_shutdown();
    return 0;
}