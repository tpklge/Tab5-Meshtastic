#include "channel_service.h"
#include "app_state.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <cassert>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>

static meshtastic_Channel radio_channels[8]{};
static std::atomic<int> writes{0};
static meshtastic_Config_LoRaConfig lora{};
static std::atomic<bool> reject_write{false};
static const uint32_t node = 0x12345678;
extern "C" void app_state_snapshot(app_snapshot_t* app) { *app={};app->state=CONN_READY;app->my_num=node; }
static void emit(const meshtastic_FromRadio& fr) {
 uint8_t raw[512];auto s=pb_ostream_from_buffer(raw,sizeof(raw));assert(pb_encode(&s,meshtastic_FromRadio_fields,&fr));channel_service_on_frame(raw,s.bytes_written);
}
static esp_err_t send(const uint8_t* data, size_t size) {
 meshtastic_ToRadio packet{};auto stream=pb_istream_from_buffer(data,size);assert(pb_decode(&stream,meshtastic_ToRadio_fields,&packet));
 const auto& p=packet.packet;assert(p.to==node && p.from==0 && p.decoded.portnum==meshtastic_PortNum_ADMIN_APP);
 meshtastic_AdminMessage request{},response{};stream=pb_istream_from_buffer(p.decoded.payload.bytes,p.decoded.payload.size);assert(pb_decode(&stream,meshtastic_AdminMessage_fields,&request));
 if(request.which_payload_variant==meshtastic_AdminMessage_get_channel_request_tag){
  assert(request.get_channel_request>=1 && request.get_channel_request<=8);
  response.which_payload_variant=meshtastic_AdminMessage_get_channel_response_tag;response.get_channel_response=radio_channels[request.get_channel_request-1];
 }else if(request.which_payload_variant==meshtastic_AdminMessage_get_config_request_tag){
  response.which_payload_variant=meshtastic_AdminMessage_get_config_response_tag;response.get_config_response.which_payload_variant=meshtastic_Config_lora_tag;
  response.get_config_response.payload_variant.lora=lora;
 }else if(request.which_payload_variant==meshtastic_AdminMessage_set_channel_tag){
  assert(request.session_passkey.size==8 && request.session_passkey.bytes[0]==42);++writes;
  if(!reject_write) radio_channels[request.set_channel.index]=request.set_channel;
  return ESP_OK;
 }else if(request.which_payload_variant==meshtastic_AdminMessage_set_config_tag){
  assert(request.session_passkey.size==8);lora=request.set_config.payload_variant.lora;return ESP_OK;
 }else { assert(false); }
 response.session_passkey.size=8;response.session_passkey.bytes[0]=42;
 meshtastic_FromRadio fr{};fr.which_payload_variant=meshtastic_FromRadio_packet_tag;fr.packet.from=node;
 fr.packet.which_payload_variant=meshtastic_MeshPacket_decoded_tag;fr.packet.decoded.portnum=meshtastic_PortNum_ADMIN_APP;fr.packet.decoded.request_id=p.id;
 auto os=pb_ostream_from_buffer(fr.packet.decoded.payload.bytes,sizeof(fr.packet.decoded.payload.bytes));assert(pb_encode(&os,meshtastic_AdminMessage_fields,&response));fr.packet.decoded.payload.size=os.bytes_written;
 // A wrong source with a matching request id must not satisfy an admin request.
 fr.packet.from=node+1;emit(fr);fr.packet.from=node;emit(fr);
 return ESP_OK;
}
static channel_snapshot_t wait_done() {
 channel_snapshot_t s{};
 for(int i=0;i<500;++i){channel_service_snapshot(&s);if(!s.busy)return s;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 assert(false);return s;
}
int main() {
 lora.region=meshtastic_Config_LoRaConfig_RegionCode_ANZ;lora.tx_power=20;lora.tx_enabled=true;
 for(int i=0;i<8;++i) radio_channels[i].index=i;
 radio_channels[0].role=meshtastic_Channel_Role_PRIMARY;radio_channels[0].has_settings=true;
 radio_channels[0].settings.psk.size=1;radio_channels[0].settings.psk.bytes[0]=1;
 assert(channel_service_init(send)==ESP_OK);
 meshtastic_FromRadio info{};info.which_payload_variant=meshtastic_FromRadio_my_info_tag;info.my_info.my_node_num=node;emit(info);
 assert(channel_service_refresh());auto snap=wait_done();for(bool known:snap.known)assert(known);assert(snap.has_lora);
 auto c=radio_channels[1];c.role=meshtastic_Channel_Role_SECONDARY;c.has_settings=true;strcpy(c.settings.name,"Equipe");c.settings.psk.size=32;memset(c.settings.psk.bytes,17,32);
 assert(channel_service_save(c));snap=wait_done();assert(snap.channels[1].settings.psk.size==32 && writes==1);
 reject_write=true;strcpy(c.settings.name,"Recusado");assert(channel_service_save(c));snap=wait_done();assert(strstr(snap.status,"nao confirmou") && !strcmp(snap.channels[1].settings.name,"Equipe"));reject_write=false;
 // Primary role cannot be removed by the editor.
 auto primary=radio_channels[0];primary.role=meshtastic_Channel_Role_DISABLED;int before=writes;
 assert(channel_service_save(primary));wait_done();assert(writes==before);
 meshtastic_ChannelSet imported{};imported.settings_count=2;strcpy(imported.settings[0].name,"Pai");imported.settings[0].psk.size=16;memset(imported.settings[0].psk.bytes,33,16);strcpy(imported.settings[1].name,"Filho");
 assert(channel_service_import(imported,false,false));snap=wait_done();assert(snap.channels[2].settings.psk.size==16 && snap.channels[3].settings.psk.size==16);assert(snap.channels[0].role==meshtastic_Channel_Role_PRIMARY);
 // Insufficient capacity is detected before any write.
 imported.settings_count=8;for(int i=0;i<8;++i)snprintf(imported.settings[i].name,12,"Novo%d",i);
 before=writes;assert(channel_service_import(imported,false,false));snap=wait_done();assert(writes==before && strstr(snap.status,"Sem espaco"));
 imported.settings_count=1;assert(channel_service_import(imported,true,false));snap=wait_done();assert(snap.channels[0].role==meshtastic_Channel_Role_PRIMARY);for(int i=1;i<8;++i)assert(snap.channels[i].role==meshtastic_Channel_Role_DISABLED);
 imported.has_lora_config=true;imported.lora_config.use_preset=true;imported.lora_config.channel_num=19;
 imported.lora_config.region=meshtastic_Config_LoRaConfig_RegionCode_US;imported.lora_config.tx_power=30;
 assert(channel_service_import(imported,true,true));snap=wait_done();assert(strstr(snap.status,"LoRa confirmados"));
 assert(lora.channel_num==19 && lora.region==meshtastic_Config_LoRaConfig_RegionCode_ANZ && lora.tx_power==20 && lora.tx_enabled);
 printf("PASS: simulated radio: correlated admin replies, session key, readback, rejection, primary invariant, add/replace, inherited keys, capacity preflight.\n");fflush(stdout);
 std::_Exit(0); // worker is intentionally a firmware-lifetime task
}
