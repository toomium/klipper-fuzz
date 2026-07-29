#ifndef IPDL_LPM_H
#define IPDL_LPM_H

#include <libprotobuf-mutator/src/mutator.h>
#include <libprotobuf-mutator/src/random.h>
#include "_mutator_factory.h"
#include <google/protobuf/descriptor.h>
#include "compile_time_request.pb.h"
#include <cctype>

const size_t g_max_size = 65535;
static bool g_debug = false;

static uint8_t *
encode_msgid(uint8_t *p, uint_fast16_t encoded_msgid)
{
    if (encoded_msgid >= 0x80)
        *p++ = (encoded_msgid >> 7) | 0x80;
    *p++ = encoded_msgid & 0x7f;
    return p;
}

static uint8_t *
encode_int(uint8_t *p, uint32_t v)
{
    int32_t sv = v;
    if (sv < (3L<<5)  && sv >= -(1L<<5))  goto f4;
    if (sv < (3L<<12) && sv >= -(1L<<12)) goto f3;
    if (sv < (3L<<19) && sv >= -(1L<<19)) goto f2;
    if (sv < (3L<<26) && sv >= -(1L<<26)) goto f1;
    *p++ = (v>>28) | 0x80;
f1: *p++ = ((v>>21) & 0x7f) | 0x80;
f2: *p++ = ((v>>14) & 0x7f) | 0x80;
f3: *p++ = ((v>>7) & 0x7f) | 0x80;
f4: *p++ = v & 0x7f;
    return p;
}


static bool is_wrapper_type(const google::protobuf::Message& msg, 
                               uint32_t& value) {
    auto desc = msg.GetDescriptor();
    auto name = desc->name();

    // Hole "content" field
    auto field = desc->FindFieldByName("content");
    if (!field) return false;
    
    auto refl = msg.GetReflection();
    
    // Prüfe ob es uint8, uint16, sint16, etc. ist
    if (name == "uint8") {
        value = refl->GetUInt32(msg, field) & 0xFF;
        return true;
    } else if( name == "uint16") {
        value = refl->GetUInt32(msg, field) & 0xFFFF;
        return true;
    } else if(name == "sint16") {
        value = static_cast<uint32_t>(static_cast<int16_t>(refl->GetInt32(msg, field)));
        return true;
    } else {
        return false;
    }
}



static uint8_t* encode_message(const google::protobuf::Message& msg, 
                                   uint8_t* buf, size_t max_size) {
    using namespace google::protobuf;
    
    auto desc = msg.GetDescriptor();
    auto refl = msg.GetReflection();
    uint8_t* p = buf;
    uint8_t* maxend = p + max_size;
    
    // Iteriere über alle Felder in Reihenfolge
    for (int i = 0; i < desc->field_count(); i++) {
        auto field = desc->field(i);
        
        if (p > maxend) {
            return p;
        }

        // Field Typ bestimmen
        switch (field->type()) {
            case FieldDescriptor::TYPE_UINT32: {
                uint32_t val = refl->GetUInt32(msg, field);
                p = encode_int(p, val);
                break;
            }
            
            case FieldDescriptor::TYPE_INT32:
            case FieldDescriptor::TYPE_SINT32: {
                int32_t val = refl->GetInt32(msg, field);
                p = encode_int(p, static_cast<uint32_t>(val));
                break;
            }
            
            case FieldDescriptor::TYPE_STRING: {
                std::string str = refl->GetString(msg, field);
                uint8_t* lenp = p++;
                size_t written = 0;
                for (char c : str) {
                    if (written >= 255) break; // Max string length
                    *p++ = static_cast<uint8_t>(c);
                    written++;
                }
                *lenp = written; // Length prefix
                break;
            }
            
            case FieldDescriptor::TYPE_BYTES: {
                std::string bytes = refl->GetString(msg, field);
                uint8_t len = std::min<size_t>(bytes.size(), 255);
                *p++ = len;
                memcpy(p, bytes.data(), len);
                p += len;
                break;
            }
            
            case FieldDescriptor::TYPE_MESSAGE: {
                // Nested message - könnte uint8/uint16 wrapper sein
                const Message& nested = refl->GetMessage(msg, field);
                
                uint32_t wrapped_value;
                is_wrapper_type(nested, wrapped_value);
                // Es ist ein Wrapper (uint8, uint16, etc.)
                p = encode_int(p, wrapped_value);
                break;
            }
            
            default:
                fprintf(stderr, "Unsupported field type: %d\n", field->type());
                break;
        }
    }
    
    return p;
}

static size_t convertSingleMessage(const google::protobuf::Message& msg, 
                        uint8_t* buf, size_t max_size, messageID id) {

    uint8_t* p = buf;
    uint8_t* start = p;

    // write msgid
    p = encode_msgid(p, static_cast<uint_fast16_t>(id));
                            
    // write parameters
    p = encode_message(msg, p, max_size);
    
    return p - start;
}

static size_t convertMultiMessage(const google::protobuf::Message& raw_seq, 
                        uint8_t* buf, size_t max_size) {
    const MultiMessage* seq = 
        dynamic_cast<const MultiMessage*>(&raw_seq);

    uint8_t* p = buf;
    uint8_t* start = p;
    size_t remaining = max_size;

    for (int i = 0; i < seq->sequence_size(); i++) {
        auto &msg = seq->sequence(i);

        // we need to map protobuf enums back to the field name
        int case_enum = msg.content_case();
        if (case_enum == 0) {
            continue;
        }
        messageID id = static_cast<messageID>(case_enum + 1);

        std::string msg_name = getMessageString(id);
        std::transform(msg_name.begin(), msg_name.end(), msg_name.begin(), ::tolower);
        
        // get matching protobuf message
        auto desc = msg.GetDescriptor();
        auto refl = msg.GetReflection();

        auto field = desc->FindFieldByName(msg_name);

        auto &msg_field = refl->GetMessage(msg, field);


        // convert to klipper
        size_t bytes_written = convertSingleMessage(msg_field, p , remaining, id);
        //size_t bytes_written = 0;
        p += bytes_written;
        remaining -= bytes_written;
    }

    return p - start;
}

/**
 * Appends all messages from the "add" protobuf message to the "dest" protobuf message
 */
static void appendProtobuf(google::protobuf::Message& dest, const google::protobuf::Message& add) {
    MultiMessage* seq = 
    dynamic_cast<MultiMessage*>(&dest);

    const MultiMessage* addSeq = 
    dynamic_cast<const MultiMessage*>(&add);

    // get messages to append 
    if (!seq || !addSeq) {
        fprintf(stderr, "[FUZZ] appendMessages: dynamic_cast failed\n");
        return;
    }

    for (int i = 0; i < addSeq->sequence_size(); i++) {
        const AnyMessage& msg = addSeq->sequence(i);
        
        if (msg.content_case() == AnyMessage::CONTENT_NOT_SET) {
            continue;
        }
        
        *seq->add_sequence() = msg; 
    }

    if (g_debug) {
        fprintf(stderr, "[FUZZ] Appended %d messages, total now: %d\n",
            addSeq->sequence_size(), seq->sequence_size());
    }
}

// base class
class MyMutatorBase {
public:
    messageID msg_id;
    bool isSingleFuzzingMode;
    uint8_t *mutated_out = nullptr;
    uint8_t *proto_buf = nullptr;
    virtual ~MyMutatorBase() {
        if (mutated_out) {
            delete[] mutated_out;
            mutated_out = nullptr;
        }
        if (proto_buf) {
            delete[] proto_buf;
            proto_buf = nullptr;
        }
    }

    MyMutatorBase() {
        mutated_out = new uint8_t[g_max_size];
        proto_buf = new uint8_t[g_max_size];
    }

    virtual bool parse(uint8_t *buf, size_t buf_size) = 0;
    virtual void mutate(size_t max_size) = 0;
    virtual std::string serialize() = 0;
    virtual void seed(unsigned int seed) = 0;
    virtual void print() = 0;
    virtual void clear() = 0;
    virtual size_t convertToKlipper(uint8_t *buf, size_t max_size) = 0;
    virtual std::minstd_rand getRandom() = 0;
    virtual void appendMessages(MyMutatorBase& other) = 0;
    virtual google::protobuf::Message* getProto() = 0;
};
 

template<typename MessageType>
class MyMutator : public MyMutatorBase, public protobuf_mutator::Mutator {
private:
    MessageType proto;
public:
    // uint8_t *mutated_out = nullptr;
    virtual ~MyMutator() {}

    bool parse(uint8_t *buf, size_t buf_size) {
        return proto.ParseFromArray(buf, buf_size);
    }
    void mutate(size_t max_size) {
        this->Mutate(&proto, max_size);
    }
    size_t convertToKlipper(uint8_t *buf, size_t max_size){
        if (isSingleFuzzingMode) {
            return convertSingleMessage(proto, buf, max_size, msg_id);
        }
        else {
            return convertMultiMessage(proto, buf, max_size);
        }
    }
    void clear(){
        proto.Clear();
    }
    google::protobuf::Message* getProto(){
        return &this->proto;
    }
    void print(){
        std::cout << proto.DebugString() << "\n";
    }
    std::string serialize() {
        return proto.SerializeAsString();
    }
    void seed(unsigned int seed) {
        this->Seed(seed);
    }
    std::minstd_rand getRandom() {
        return static_cast<std::minstd_rand>(*this->random());
    }

    void appendMessages(MyMutatorBase& other) {
        appendProtobuf(this->proto, *other.getProto());
    }
};

#endif