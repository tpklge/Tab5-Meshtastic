#include "SafeFile.h"
#include "meshtastic/deviceonly.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include <cstdio>
// Use the RAK's exact generated ChannelFile layout, bounded to eight channels.
PB_BIND(meshtastic_ChannelFile, meshtastic_ChannelFile, 2)
static std::vector<uint8_t> encode(bool privateChannel) {
    meshtastic_ChannelFile c{}; c.version=24;c.channels_count=8;
    for(int i=0;i<8;++i)c.channels[i].index=i;
    c.channels[0].has_settings=true;c.channels[0].role=meshtastic_Channel_Role_PRIMARY;
    if(privateChannel){c.channels[1].has_settings=true;c.channels[1].role=meshtastic_Channel_Role_SECONDARY;strcpy(c.channels[1].settings.name,"Equipe");}
    std::vector<uint8_t> bytes(meshtastic_ChannelFile_size);
    auto stream=pb_ostream_from_buffer(bytes.data(),bytes.size());assert(pb_encode(&stream,meshtastic_ChannelFile_fields,&c));bytes.resize(stream.bytes_written);return bytes;
}
int main(int argc,char**) {
    const bool fixed=argc>1;
    const char* name="/prefs/channels.proto"; auto old=encode(false), changed=encode(true);
    fs.files[name]=old;
    SafeFile save(name,true);assert(save.write(changed.data(),changed.size())==changed.size());assert(save.close());
    auto& stored=fs.files[name]; meshtastic_ChannelFile loaded{};
    auto stream=pb_istream_from_buffer(stored.data(),stored.size());bool decoded=pb_decode(&stream,meshtastic_ChannelFile_fields,&loaded);
    if(!fixed) {
        assert(stored.size()==old.size()+changed.size()); assert(!decoded);
        printf("REPRODUCED: old + new file (%zu bytes), nanopb: %s\n",stored.size(),PB_GET_ERROR(&stream));return 0;
    }
    assert(stored==changed && decoded && loaded.channels_count==8 && !strcmp(loaded.channels[1].settings.name,"Equipe"));
    // Stale temporary data must never be appended on retry.
    fs.files[std::string(name)+".tmp"]={0xFF,0xFF};
    SafeFile retry(name,true);retry.write(old.data(),old.size());assert(retry.close());assert(fs.files[name]==old);
    // A failed replacement must keep the committed preferences intact.
    fs.failRename=true;SafeFile fail(name,true);fail.write(changed.data(),changed.size());assert(!fail.close());assert(fs.files[name]==old);
    fs.failRename=false;fs.failRemove=true;SafeFile stale(name,true);assert(stale.write(changed.data(),changed.size())==0);assert(!stale.close());assert(fs.files[name]==old);
    puts("PASS: replacement, decode after reopen, stale temp recovery, failed rename/removal preserve old file.");
}
