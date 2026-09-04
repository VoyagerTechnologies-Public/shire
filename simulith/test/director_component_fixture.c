#include "simulith_component.h"

#include <stddef.h>

#if defined(DIRECTOR_FIXTURE_NO_INTERFACE)

int director_fixture_without_interface(void)
{
    return 0;
}

#else

static const component_interface_t fixture_interface = {
    .name = "director_fixture",
    .description = "Director plugin-loading test fixture",
    .init = NULL,
    .tick = NULL,
    .cleanup = NULL,
    .backdoor = NULL,
};

__attribute__((visibility("default")))
const component_interface_t *get_component_interface(void)
{
#if defined(DIRECTOR_FIXTURE_NULL_INTERFACE)
    return NULL;
#else
    return &fixture_interface;
#endif
}

#endif
