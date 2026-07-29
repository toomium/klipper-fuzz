#include <libprotobuf-mutator/src/mutator.h>
#include <libprotobuf-mutator/src/random.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <google/protobuf/stubs/common.h>
#include "mutator.h"
#include "_mutator_factory.h"

using std::cin;
using std::cout;
using std::endl;




/**
 * Initialize this custom mutator
 *
 * @param[in] afl a pointer to the internal state object. Can be ignored for
 * now.
 * @param[in] seed A seed for this mutator - the same seed should always mutate
 * in the same way.
 * @return Pointer to the data object this custom mutator instance should use.
 *         There may be multiple instances of this mutator in one afl-fuzz run!
 *         Return NULL on error.
 */
extern "C" MyMutatorBase* afl_custom_init(void *afl, unsigned int seed) {
    // find out which message we are going to fuzz in this run
    const char* env_var = std::getenv("SET_TARGET");

    messageID g_msg_id;
    bool isSingleFuzzingMode;
    if (!!env_var) {
        g_msg_id = getMessageId(env_var);
        if (strcmp(env_var, "MULTIMESSAGE") == 0) {
            isSingleFuzzingMode = false;
        }
        else {
            isSingleFuzzingMode = true; 
        }
    } else {
        isSingleFuzzingMode = false;
    }

    if (isSingleFuzzingMode){
        fprintf(stderr, "[MUTATOR] Selected Single fuzzing mode");
        fprintf(stderr, "[MUTATOR] Target: %s\n", getMessageString(g_msg_id).c_str());
        fprintf(stderr, "[MUTATOR] message id: %u\n", g_msg_id);
    }
    else {
        fprintf(stderr, "[MUTATOR] Selected Multi fuzzing mode");
    }

    g_debug = !!std::getenv("DEBUG");

    // get matching mutator from factory
    MyMutatorBase* mutator = getMutator(g_msg_id);

    mutator->msg_id = g_msg_id;
    mutator->isSingleFuzzingMode = isSingleFuzzingMode;

    // silent any warnings that occur because we later serialize a protobuf object containing non utf8 characters
    google::protobuf::SetLogHandler([](auto...) {});

    // set seed
    mutator->seed(seed);
    return mutator;
}

/**
 * Perform custom mutations on a given input
 *
 * @param[in] data pointer returned in afl_custom_init for this fuzz case
 * @param[in] buf Pointer to input data to be mutated
 * @param[in] buf_size Size of input data
 * @param[out] out_buf the buffer we will work on. we can reuse *buf. NULL on
 * error.
 * @param[in] add_buf Buffer containing the additional test case
 * @param[in] add_buf_size Size of the additional test case
 * @param[in] max_size Maximum size of the mutated output. The mutation must not
 *     produce data larger than max_size.
 * @return Size of the mutated output.
 */
extern "C" size_t afl_custom_fuzz(MyMutatorBase *mutator, // return value from afl_custom_init
                       uint8_t *buf, size_t buf_size, // input data to be mutated
                       uint8_t **out_buf, // output buffer
                       uint8_t *add_buf, size_t add_buf_size,  // add_buf can be NULL
                       size_t max_size) {

    if (g_debug) {
        fprintf(stderr, "[FUZZ] Got raw Bytes:");
        for (size_t i = 0; i < buf_size; i++) {
            fprintf(stderr, "%02x ", (buf)[i]);
        }
        fprintf(stderr, "\n");
    }

    // parse input buffer into protobuf
    bool valid = mutator->parse(buf, buf_size);
    if(!valid) {
        // invalid input -> not a valid seed
        mutator->clear();
        if (g_debug) {
            fprintf(stderr, "[FUZZ] Invalid protobuf input, cleared to defaults\n");
        }
    }

    if (g_debug) {
        fprintf(stderr, "[FUZZ] Before mutations:\n");
        mutator->print();
    }

    if (!mutator->isSingleFuzzingMode
        && add_buf != nullptr
        && add_buf_size > 0
        && (mutator->getRandom()() % 100) < 40)
    {
        MyMutatorBase* tmp = getMutator(mutator->msg_id);

        if (tmp->parse(add_buf, add_buf_size)) {
            mutator->appendMessages(*tmp);
            if (g_debug) fprintf(stderr, "[FUZZ] Spliced add_buf messages into sequence\n");
        }
        delete tmp;
    }

    // mutate protobuf
    mutator->mutate(max_size);

    if (g_debug) {
        fprintf(stderr, "[FUZZ] After mutations:\n");
        mutator->print();
    }


    // serialize
    std::string proto = mutator->serialize();
    if (g_debug) {
        fprintf(stderr, "[FUZZ] Serialized to Protostring: %s\n", proto.c_str());
    }


    // copy string to proto_buf of custom mutator
    size_t mutated_size = std::min(proto.length(), g_max_size);
    memcpy(mutator->proto_buf, proto.c_str(), mutated_size);

    // return
    size_t returned_size = std::min(mutated_size, max_size);
    *out_buf = mutator->proto_buf;

    if (g_debug) {
        fprintf(stderr, "[FUZZ] Produced raw Bytes:");
        for (size_t i = 0; i < returned_size; i++) {
            fprintf(stderr, "%02x ", (*out_buf)[i]);
        }
        fprintf(stderr, "\n");
    }

    return returned_size;
}

/**
 * A post-processing function to use right before AFL writes the test case to
 * disk in order to execute the target.
 *
 * (Optional) If this functionality is not needed, simply don't define this
 * function.
 *
 * @param[in] data pointer returned in afl_custom_init for this fuzz case
 * @param[in] buf Buffer containing the test case to be executed
 * @param[in] buf_size Size of the test case
 * @param[out] out_buf Pointer to the buffer containing the test case after
 *     processing. External library should allocate memory for out_buf.
 *     The buf pointer may be reused (up to the given buf_size);
 * @return Size of the output buffer after processing or the needed amount.
 *     A return of 0 indicates an error.
 */
extern "C" size_t afl_custom_post_process(MyMutatorBase *mutator, uint8_t *buf,
                                          size_t buf_size, uint8_t **out_buf) {

    if (g_debug) {
        fprintf(stderr, "[POST] Got raw Bytes:");
        for (size_t i = 0; i < buf_size; i++) {
            fprintf(stderr, "%02x ", (buf)[i]);
        }
        fprintf(stderr, "\n");
    }

    // parse input buffer into protobuf
    bool valid = mutator->parse(buf, buf_size);
    if(!valid) {
        // invalid input -> not a valid seed
        return 0;
    }

    if (g_debug) {
        mutator->print();
    }

    size_t klipper_size = mutator->convertToKlipper(mutator->mutated_out, g_max_size);

    *out_buf = mutator->mutated_out;


    if (g_debug) {
        // Hex dump
        fprintf(stderr, "[POST] Produced raw bytes: ");
        for (size_t i = 0; i < std::min(klipper_size, (size_t)64); i++) {
            fprintf(stderr, "%02x ", (*out_buf)[i]);
        }
        fprintf(stderr, "\n");
    }


    return klipper_size;
}


/**
 * Deinitialize everything
 *
 * @param data The data ptr from afl_custom_init
 */
extern "C" void afl_custom_deinit(MyMutatorBase *mutator) {
    delete mutator;
}