#ifndef SERVICE_API_H
#define SERVICE_API_H

#include "player_controller.h"

#include <stdint.h>

enum
{
  SAWIT_SERVICE_MAX_REMOTE_PLAYERS = 16,
  SAWIT_SERVICE_ADDRESS_LENGTH = 128,
  SAWIT_SERVICE_NAME_LENGTH = 64,
  SAWIT_SERVICE_TOKEN_LENGTH = 96,
  SAWIT_SERVICE_ERROR_LENGTH = 160
};

typedef struct SawitRemotePlayer
{
  uint64_t player_id;
  float x;
  float y;
  float z;
  float velocity_x;
  float velocity_y;
  float velocity_z;
  float yaw;
  float pitch;
  unsigned int last_processed_input;
  int grounded;
  int active;
} SawitRemotePlayer;

typedef struct SawitServiceClient
{
  int enabled;
  int socket_open;
  int connected;
  int control_plane_enabled;
  int control_plane_joined;
  uint64_t player_id;
  uint64_t room_id;
  unsigned short tick_rate_hz;
  unsigned short snapshot_rate_hz;
  unsigned int input_seq;
  unsigned int client_tick;
  unsigned int last_snapshot_tick;
  unsigned int last_applied_snapshot_tick;
  uint64_t last_pong_rtt_ms;
  float input_accumulator_seconds;
  float hello_accumulator_seconds;
  float ping_accumulator_seconds;
  int remote_player_count;
  SawitRemotePlayer local_authority;
  int local_authority_valid;
  SawitRemotePlayer remote_players[SAWIT_SERVICE_MAX_REMOTE_PLAYERS];
  char control_address[SAWIT_SERVICE_ADDRESS_LENGTH];
  char server_address[SAWIT_SERVICE_ADDRESS_LENGTH];
  char join_token[SAWIT_SERVICE_TOKEN_LENGTH];
  char player_name[SAWIT_SERVICE_NAME_LENGTH];
  char last_error[SAWIT_SERVICE_ERROR_LENGTH];
  void* native_state;
} SawitServiceClient;

void sawit_service_client_init(SawitServiceClient* client);
void sawit_service_client_shutdown(SawitServiceClient* client);
void sawit_service_client_update(
  SawitServiceClient* client,
  const PlayerController* player,
  const PlatformInput* input,
  float delta_seconds
);
int sawit_service_client_apply_authority(SawitServiceClient* client, PlayerController* player);

#endif
