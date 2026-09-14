#include "simulith_component.h"

#include <stddef.h>

#if defined(DIRECTOR_FIXTURE_NO_INTERFACE)

int director_fixture_without_interface(void)
{
    return 0;
}

#else

struct component_state
{
    int unused;
};

static struct component_state fixture_state;

static int fixture_create(component_state_t **state)
{
    *state = (component_state_t *)&fixture_state;
    return COMPONENT_SUCCESS;
}

static void fixture_destroy(component_state_t *state)
{
    (void)state;
}

static const component_interface_t fixture_interface = {
    .api_version =
#if defined(DIRECTOR_FIXTURE_BAD_VERSION)
        SIMULITH_COMPONENT_API_VERSION + 1U,
#else
        SIMULITH_COMPONENT_API_VERSION,
#endif
    .struct_size =
#if defined(DIRECTOR_FIXTURE_BAD_SIZE)
        sizeof(component_interface_t) - 1U,
#else
        sizeof(component_interface_t),
#endif
    .name = "director_fixture",
    .description = "Director plugin-loading test fixture",
    .create = fixture_create,
    .on_tick = NULL,
    .service = NULL,
    .actuate = NULL,
    .destroy = fixture_destroy,
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
