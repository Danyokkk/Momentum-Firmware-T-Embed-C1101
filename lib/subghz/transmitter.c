#include "transmitter.h"

#include "protocols/base.h"
#include "registry.h"

struct SubGhzTransmitter {
    const SubGhzProtocol* protocol;
    SubGhzProtocolEncoderBase* protocol_instance;
};

SubGhzTransmitter*
    subghz_transmitter_alloc_init(SubGhzEnvironment* environment, const char* protocol_name) {
    SubGhzTransmitter* instance = NULL;
    const SubGhzProtocolRegistry* protocol_registry_items =
        subghz_environment_get_protocol_registry(environment);

    const SubGhzProtocol* protocol =
        subghz_protocol_registry_get_by_name(protocol_registry_items, protocol_name);

#ifdef FURI_PROFILE_LAB
    /* Lab profile: rolling-code (SubGhzProtocolTypeDynamic) protocols are
     * receive/decode only. Creating their encoder is what generates the next
     * valid code for a KeeLoq, Security+, Somfy... remote, whether for "Add
     * Manually", for sending a saved file or for an external app, so it is
     * refused here once for all of them. Static-code and RAW protocols are not
     * affected. */
    if(protocol && protocol->type == SubGhzProtocolTypeDynamic) {
        return NULL;
    }
#endif
    if(protocol && protocol->encoder && protocol->encoder->alloc) {
        instance = malloc(sizeof(SubGhzTransmitter));
        instance->protocol = protocol;
        instance->protocol_instance = instance->protocol->encoder->alloc(environment);
    }
    return instance;
}

void subghz_transmitter_free(SubGhzTransmitter* instance) {
    furi_check(instance);
    instance->protocol->encoder->free(instance->protocol_instance);
    free(instance);
}

SubGhzProtocolEncoderBase* subghz_transmitter_get_protocol_instance(SubGhzTransmitter* instance) {
    furi_check(instance);
    return instance->protocol_instance;
}

bool subghz_transmitter_stop(SubGhzTransmitter* instance) {
    furi_check(instance);
    bool ret = false;
    if(instance->protocol && instance->protocol->encoder && instance->protocol->encoder->stop) {
        instance->protocol->encoder->stop(instance->protocol_instance);
        ret = true;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_transmitter_deserialize(SubGhzTransmitter* instance, FlipperFormat* flipper_format) {
    furi_check(instance);
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    if(instance->protocol && instance->protocol->encoder &&
       instance->protocol->encoder->deserialize) {
        ret =
            instance->protocol->encoder->deserialize(instance->protocol_instance, flipper_format);
    }
    return ret;
}

LevelDuration subghz_transmitter_yield(void* context) {
    SubGhzTransmitter* instance = context;
    return instance->protocol->encoder->yield(instance->protocol_instance);
}
