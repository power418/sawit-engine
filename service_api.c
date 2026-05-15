#if defined(_WIN32)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "service_api.h"

#include "diagnostics.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
typedef SOCKET SawitSocketHandle;
#define SAWIT_INVALID_SOCKET_HANDLE INVALID_SOCKET
#else
typedef int SawitSocketHandle;
#define SAWIT_INVALID_SOCKET_HANDLE (-1)
#endif

typedef struct SawitServiceNative
{
  SawitSocketHandle socket_handle;
  int winsock_started;
} SawitServiceNative;

static const unsigned short k_sawit_protocol_version = 1;
static const float k_sawit_input_send_interval_seconds = 1.0f / 30.0f;
static const float k_sawit_hello_retry_seconds = 1.0f;
static const float k_sawit_ping_interval_seconds = 1.0f;
static const int k_sawit_control_timeout_ms = 1800;

static int sawit_service_is_disabled(void);
static int sawit_service_is_off_value(const char* value);
static int sawit_service_copy_text(char* destination, size_t destination_size, const char* source);
static int sawit_service_parse_room_id(uint64_t* out_room_id);
static void sawit_service_pick_player_name(char* out_name, size_t out_name_size);
static int sawit_service_startup_network(int* out_started);
static void sawit_service_cleanup_network(int started);
static void sawit_service_close_handle(SawitSocketHandle socket_handle);
static int sawit_service_set_socket_timeout(SawitSocketHandle socket_handle, int timeout_ms);
static int sawit_service_join_control_plane(SawitServiceClient* client, const char* control_address);
static int sawit_service_parse_join_response(SawitServiceClient* client, char* response);
static int sawit_service_send_all(SawitSocketHandle socket_handle, const char* text);
static int sawit_service_recv_line(SawitSocketHandle socket_handle, char* out_line, size_t out_line_size);
static int sawit_service_open_socket(SawitServiceClient* client, const char* address);
static void sawit_service_close_socket(SawitServiceClient* client);
static int sawit_service_split_address(const char* address, char* out_host, size_t host_size, char* out_port, size_t port_size);
static int sawit_service_set_nonblocking(SawitSocketHandle socket_handle);
static int sawit_service_send_text(SawitServiceClient* client, const char* text);
static void sawit_service_send_hello(SawitServiceClient* client);
static void sawit_service_send_disconnect(SawitServiceClient* client);
static void sawit_service_send_input(
  SawitServiceClient* client,
  const PlayerController* player,
  const PlatformInput* input,
  unsigned int dt_ms
);
static void sawit_service_send_ping(SawitServiceClient* client);
static void sawit_service_recv_packets(SawitServiceClient* client);
static void sawit_service_parse_packet(SawitServiceClient* client, char* packet);
static void sawit_service_parse_welcome(SawitServiceClient* client, char* packet);
static void sawit_service_parse_snapshot(SawitServiceClient* client, char* packet);
static void sawit_service_parse_pong(SawitServiceClient* client, char* packet);
static char* sawit_service_next_token(char** cursor);
static int sawit_service_parse_u64(const char* text, uint64_t* out_value);
static int sawit_service_parse_u32(const char* text, unsigned int* out_value);
static int sawit_service_parse_u16(const char* text, unsigned short* out_value);
static int sawit_service_parse_float(const char* text, float* out_value);
static uint64_t sawit_service_now_ms(void);

void sawit_service_client_init(SawitServiceClient* client)
{
  const char* udp_address = NULL;
  const char* control_address = NULL;

  if (client == NULL)
  {
    return;
  }

  memset(client, 0, sizeof(*client));
  if (sawit_service_is_disabled())
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "SAWIT_MULTIPLAYER=0");
    return;
  }

  client->enabled = 1;
  client->control_plane_enabled = 1;
  client->room_id = 1U;
  (void)sawit_service_parse_room_id(&client->room_id);
  sawit_service_pick_player_name(client->player_name, sizeof(client->player_name));

  udp_address = getenv("SAWIT_SERVICE_ADDR");
  if (udp_address == NULL || udp_address[0] == '\0')
  {
    udp_address = "127.0.0.1:4000";
  }
  sawit_service_copy_text(client->server_address, sizeof(client->server_address), udp_address);

  control_address = getenv("SAWIT_CONTROL_ADDR");
  if (control_address == NULL || control_address[0] == '\0')
  {
    control_address = "127.0.0.1:4001";
  }

  if (sawit_service_is_off_value(control_address))
  {
    client->control_plane_enabled = 0;
    sawit_service_copy_text(client->control_address, sizeof(client->control_address), "off");
  }
  else
  {
    sawit_service_copy_text(client->control_address, sizeof(client->control_address), control_address);
    if (sawit_service_join_control_plane(client, client->control_address))
    {
      client->control_plane_joined = 1;
      diagnostics_logf(
        "sawit_service: tcp control joined control=%s udp=%s room=%llu token=%s",
        client->control_address,
        client->server_address,
        (unsigned long long)client->room_id,
        client->join_token[0] != '\0' ? client->join_token : "none");
    }
    else
    {
      diagnostics_logf(
        "sawit_service: tcp control unavailable (%s), fallback_udp=%s",
        client->last_error[0] != '\0' ? client->last_error : "unknown",
        client->server_address);
    }
  }

  if (!sawit_service_open_socket(client, client->server_address))
  {
    client->enabled = 0;
    diagnostics_logf("sawit_service: disabled (%s)", client->last_error);
    return;
  }

  diagnostics_logf(
    "sawit_service: udp connecting addr=%s room=%llu name=%s control=%s",
    client->server_address,
    (unsigned long long)client->room_id,
    client->player_name,
    client->control_plane_joined ? "joined" : (client->control_plane_enabled ? "fallback" : "off"));
  sawit_service_send_hello(client);
}

void sawit_service_client_shutdown(SawitServiceClient* client)
{
  if (client == NULL)
  {
    return;
  }

  sawit_service_send_disconnect(client);
  sawit_service_close_socket(client);
  memset(client, 0, sizeof(*client));
}

void sawit_service_client_update(
  SawitServiceClient* client,
  const PlayerController* player,
  const PlatformInput* input,
  float delta_seconds
)
{
  if (client == NULL || !client->enabled || !client->socket_open)
  {
    return;
  }

  if (delta_seconds < 0.0f)
  {
    delta_seconds = 0.0f;
  }
  if (delta_seconds > 0.25f)
  {
    delta_seconds = 0.25f;
  }

  sawit_service_recv_packets(client);

  if (!client->connected)
  {
    client->hello_accumulator_seconds += delta_seconds;
    if (client->hello_accumulator_seconds >= k_sawit_hello_retry_seconds)
    {
      client->hello_accumulator_seconds = 0.0f;
      sawit_service_send_hello(client);
    }
    return;
  }

  client->input_accumulator_seconds += delta_seconds;
  while (client->input_accumulator_seconds >= k_sawit_input_send_interval_seconds)
  {
    client->input_accumulator_seconds -= k_sawit_input_send_interval_seconds;
    sawit_service_send_input(
      client,
      player,
      input,
      (unsigned int)(k_sawit_input_send_interval_seconds * 1000.0f + 0.5f));
  }

  client->ping_accumulator_seconds += delta_seconds;
  if (client->ping_accumulator_seconds >= k_sawit_ping_interval_seconds)
  {
    client->ping_accumulator_seconds = 0.0f;
    sawit_service_send_ping(client);
  }
}

int sawit_service_client_apply_authority(SawitServiceClient* client, PlayerController* player)
{
  if (client == NULL || player == NULL || !client->connected || !client->local_authority_valid)
  {
    return 0;
  }
  if (client->last_applied_snapshot_tick == client->last_snapshot_tick)
  {
    return 0;
  }

  player->camera.x = client->local_authority.x;
  player->camera.z = client->local_authority.z;
  client->last_applied_snapshot_tick = client->last_snapshot_tick;
  return 1;
}

static int sawit_service_is_disabled(void)
{
  const char* value = getenv("SAWIT_MULTIPLAYER");
  return sawit_service_is_off_value(value);
}

static int sawit_service_is_off_value(const char* value)
{
  return value != NULL &&
    (strcmp(value, "0") == 0 ||
      strcmp(value, "off") == 0 ||
      strcmp(value, "OFF") == 0 ||
      strcmp(value, "false") == 0 ||
      strcmp(value, "FALSE") == 0 ||
      strcmp(value, "disabled") == 0 ||
      strcmp(value, "DISABLED") == 0);
}

static int sawit_service_copy_text(char* destination, size_t destination_size, const char* source)
{
  size_t length = 0U;

  if (destination == NULL || destination_size == 0U)
  {
    return 0;
  }

  destination[0] = '\0';
  if (source == NULL)
  {
    return 1;
  }

  length = strlen(source);
  if (length >= destination_size)
  {
    length = destination_size - 1U;
  }
  memcpy(destination, source, length);
  destination[length] = '\0';
  return 1;
}

static int sawit_service_parse_room_id(uint64_t* out_room_id)
{
  const char* room_text = getenv("SAWIT_ROOM_ID");
  uint64_t room_id = 0U;

  if (out_room_id == NULL || room_text == NULL || room_text[0] == '\0')
  {
    return 0;
  }
  if (!sawit_service_parse_u64(room_text, &room_id) || room_id == 0U)
  {
    return 0;
  }

  *out_room_id = room_id;
  return 1;
}

static void sawit_service_pick_player_name(char* out_name, size_t out_name_size)
{
  const char* name = getenv("SAWIT_PLAYER_NAME");

  if (name == NULL || name[0] == '\0')
  {
    name = getenv("USERNAME");
  }
  if (name == NULL || name[0] == '\0')
  {
    name = getenv("USER");
  }
  if (name == NULL || name[0] == '\0')
  {
    name = "sawit-engine";
  }

  sawit_service_copy_text(out_name, out_name_size, name);
}

static int sawit_service_startup_network(int* out_started)
{
  if (out_started != NULL)
  {
    *out_started = 0;
  }

#if defined(_WIN32)
  {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
      return 0;
    }
    if (out_started != NULL)
    {
      *out_started = 1;
    }
  }
#endif

  return 1;
}

static void sawit_service_cleanup_network(int started)
{
#if defined(_WIN32)
  if (started != 0)
  {
    WSACleanup();
  }
#else
  (void)started;
#endif
}

static void sawit_service_close_handle(SawitSocketHandle socket_handle)
{
  if (socket_handle == SAWIT_INVALID_SOCKET_HANDLE)
  {
    return;
  }

#if defined(_WIN32)
  closesocket(socket_handle);
#else
  close(socket_handle);
#endif
}

static int sawit_service_set_socket_timeout(SawitSocketHandle socket_handle, int timeout_ms)
{
#if defined(_WIN32)
  DWORD timeout_value = (DWORD)((timeout_ms > 0) ? timeout_ms : 1);
  return setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_value, sizeof(timeout_value)) == 0 &&
    setsockopt(socket_handle, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_value, sizeof(timeout_value)) == 0;
#else
  struct timeval timeout_value;
  timeout_value.tv_sec = timeout_ms / 1000;
  timeout_value.tv_usec = (timeout_ms % 1000) * 1000;
  if (timeout_value.tv_sec == 0 && timeout_value.tv_usec <= 0)
  {
    timeout_value.tv_usec = 1000;
  }
  return setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &timeout_value, sizeof(timeout_value)) == 0 &&
    setsockopt(socket_handle, SOL_SOCKET, SO_SNDTIMEO, &timeout_value, sizeof(timeout_value)) == 0;
#endif
}

static int sawit_service_join_control_plane(SawitServiceClient* client, const char* control_address)
{
  struct addrinfo hints;
  struct addrinfo* result = NULL;
  struct addrinfo* candidate = NULL;
  SawitSocketHandle socket_handle = SAWIT_INVALID_SOCKET_HANDLE;
  char host[96] = { 0 };
  char port[16] = { 0 };
  char command[192] = { 0 };
  char line[512] = { 0 };
  int network_started = 0;
  int joined = 0;

  if (client == NULL || control_address == NULL || control_address[0] == '\0')
  {
    return 0;
  }
  if (!sawit_service_split_address(control_address, host, sizeof(host), port, sizeof(port)))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "bad SAWIT_CONTROL_ADDR");
    return 0;
  }
  if (!sawit_service_startup_network(&network_started))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "network startup failed");
    return 0;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(host, port, &hints, &result) != 0)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "control getaddrinfo failed");
    sawit_service_cleanup_network(network_started);
    return 0;
  }

  for (candidate = result; candidate != NULL; candidate = candidate->ai_next)
  {
    socket_handle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (socket_handle == SAWIT_INVALID_SOCKET_HANDLE)
    {
      continue;
    }

    (void)sawit_service_set_socket_timeout(socket_handle, k_sawit_control_timeout_ms);
    if (connect(socket_handle, candidate->ai_addr, (int)candidate->ai_addrlen) == 0)
    {
      break;
    }

    sawit_service_close_handle(socket_handle);
    socket_handle = SAWIT_INVALID_SOCKET_HANDLE;
  }

  freeaddrinfo(result);
  if (socket_handle == SAWIT_INVALID_SOCKET_HANDLE)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "control connect failed");
    sawit_service_cleanup_network(network_started);
    return 0;
  }

  (void)sawit_service_recv_line(socket_handle, line, sizeof(line));
  (void)snprintf(
    command,
    sizeof(command),
    "JOIN %llu %s\n",
    (unsigned long long)client->room_id,
    client->player_name);

  if (!sawit_service_send_all(socket_handle, command))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "control send JOIN failed");
  }
  else if (!sawit_service_recv_line(socket_handle, line, sizeof(line)))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "control JOIN timeout");
  }
  else if (sawit_service_parse_join_response(client, line))
  {
    joined = 1;
  }

  (void)sawit_service_send_all(socket_handle, "QUIT\n");
  sawit_service_close_handle(socket_handle);
  sawit_service_cleanup_network(network_started);
  return joined;
}

static int sawit_service_parse_join_response(SawitServiceClient* client, char* response)
{
  char* cursor = response;
  char* token = sawit_service_next_token(&cursor);
  int saw_udp_addr = 0;

  if (client == NULL || token == NULL || strcmp(token, "JOIN_OK") != 0)
  {
    sawit_service_copy_text(client != NULL ? client->last_error : NULL, client != NULL ? sizeof(client->last_error) : 0U, "control JOIN rejected");
    return 0;
  }

  while ((token = sawit_service_next_token(&cursor)) != NULL)
  {
    char* equals = strchr(token, '=');
    const char* key = token;
    const char* value = NULL;

    if (equals == NULL)
    {
      continue;
    }
    *equals = '\0';
    value = equals + 1;

    if (strcmp(key, "room_id") == 0)
    {
      uint64_t room_id = 0U;
      if (sawit_service_parse_u64(value, &room_id) && room_id != 0U)
      {
        client->room_id = room_id;
      }
    }
    else if (strcmp(key, "udp_addr") == 0)
    {
      sawit_service_copy_text(client->server_address, sizeof(client->server_address), value);
      saw_udp_addr = 1;
    }
    else if (strcmp(key, "join_token") == 0)
    {
      sawit_service_copy_text(client->join_token, sizeof(client->join_token), value);
    }
    else if (strcmp(key, "tick_rate_hz") == 0)
    {
      (void)sawit_service_parse_u16(value, &client->tick_rate_hz);
    }
    else if (strcmp(key, "snapshot_rate_hz") == 0)
    {
      (void)sawit_service_parse_u16(value, &client->snapshot_rate_hz);
    }
  }

  if (!saw_udp_addr)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "control JOIN missing udp_addr");
    return 0;
  }

  client->last_error[0] = '\0';
  return 1;
}

static int sawit_service_send_all(SawitSocketHandle socket_handle, const char* text)
{
  const char* cursor = text;
  size_t remaining = (text != NULL) ? strlen(text) : 0U;

  while (remaining > 0U)
  {
    int sent = 0;

#if defined(_WIN32)
    sent = send(socket_handle, cursor, (int)remaining, 0);
#else
    sent = (int)send(socket_handle, cursor, remaining, 0);
#endif
    if (sent <= 0)
    {
      return 0;
    }
    cursor += sent;
    remaining -= (size_t)sent;
  }

  return 1;
}

static int sawit_service_recv_line(SawitSocketHandle socket_handle, char* out_line, size_t out_line_size)
{
  size_t used = 0U;

  if (out_line == NULL || out_line_size == 0U)
  {
    return 0;
  }

  out_line[0] = '\0';
  while (used + 1U < out_line_size)
  {
    char c = '\0';
    int received = 0;

#if defined(_WIN32)
    received = recv(socket_handle, &c, 1, 0);
#else
    received = (int)recv(socket_handle, &c, 1, 0);
#endif
    if (received <= 0)
    {
      return used > 0U;
    }
    if (c == '\n')
    {
      break;
    }
    if (c != '\r')
    {
      out_line[used] = c;
      used += 1U;
    }
  }

  out_line[used] = '\0';
  return used > 0U;
}

static int sawit_service_open_socket(SawitServiceClient* client, const char* address)
{
  SawitServiceNative* native_state = NULL;
  struct addrinfo hints;
  struct addrinfo* result = NULL;
  struct addrinfo* candidate = NULL;
  char host[96] = { 0 };
  char port[16] = { 0 };
  int resolved = 0;

  if (client == NULL || address == NULL)
  {
    return 0;
  }

  native_state = (SawitServiceNative*)calloc(1U, sizeof(*native_state));
  if (native_state == NULL)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "out of memory");
    return 0;
  }
  native_state->socket_handle = SAWIT_INVALID_SOCKET_HANDLE;

#if defined(_WIN32)
  {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
      sawit_service_copy_text(client->last_error, sizeof(client->last_error), "WSAStartup failed");
      free(native_state);
      return 0;
    }
    native_state->winsock_started = 1;
  }
#endif

  if (!sawit_service_split_address(address, host, sizeof(host), port, sizeof(port)))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "bad SAWIT_SERVICE_ADDR");
    client->native_state = native_state;
    sawit_service_close_socket(client);
    return 0;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  if (getaddrinfo(host, port, &hints, &result) != 0)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "getaddrinfo failed");
    client->native_state = native_state;
    sawit_service_close_socket(client);
    return 0;
  }

  for (candidate = result; candidate != NULL; candidate = candidate->ai_next)
  {
    native_state->socket_handle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (native_state->socket_handle == SAWIT_INVALID_SOCKET_HANDLE)
    {
      continue;
    }

    if (connect(native_state->socket_handle, candidate->ai_addr, (int)candidate->ai_addrlen) == 0 &&
      sawit_service_set_nonblocking(native_state->socket_handle))
    {
      resolved = 1;
      break;
    }

#if defined(_WIN32)
    closesocket(native_state->socket_handle);
#else
    close(native_state->socket_handle);
#endif
    native_state->socket_handle = SAWIT_INVALID_SOCKET_HANDLE;
  }

  freeaddrinfo(result);
  if (!resolved)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "socket connect failed");
    client->native_state = native_state;
    sawit_service_close_socket(client);
    return 0;
  }

  client->native_state = native_state;
  client->socket_open = 1;
  return 1;
}

static void sawit_service_close_socket(SawitServiceClient* client)
{
  SawitServiceNative* native_state = NULL;

  if (client == NULL)
  {
    return;
  }

  native_state = (SawitServiceNative*)client->native_state;
  if (native_state == NULL)
  {
    return;
  }

  if (native_state->socket_handle != SAWIT_INVALID_SOCKET_HANDLE)
  {
#if defined(_WIN32)
    closesocket(native_state->socket_handle);
#else
    close(native_state->socket_handle);
#endif
    native_state->socket_handle = SAWIT_INVALID_SOCKET_HANDLE;
  }

#if defined(_WIN32)
  if (native_state->winsock_started != 0)
  {
    WSACleanup();
  }
#endif

  free(native_state);
  client->native_state = NULL;
  client->socket_open = 0;
  client->connected = 0;
}

static int sawit_service_split_address(const char* address, char* out_host, size_t host_size, char* out_port, size_t port_size)
{
  const char* colon = NULL;
  size_t host_length = 0U;

  if (address == NULL || out_host == NULL || out_port == NULL || host_size == 0U || port_size == 0U)
  {
    return 0;
  }

  colon = strrchr(address, ':');
  if (colon == NULL || colon == address || colon[1] == '\0')
  {
    return 0;
  }

  host_length = (size_t)(colon - address);
  if (host_length >= host_size)
  {
    host_length = host_size - 1U;
  }

  memcpy(out_host, address, host_length);
  out_host[host_length] = '\0';
  sawit_service_copy_text(out_port, port_size, colon + 1);
  return out_host[0] != '\0' && out_port[0] != '\0';
}

static int sawit_service_set_nonblocking(SawitSocketHandle socket_handle)
{
#if defined(_WIN32)
  u_long mode = 1UL;
  return ioctlsocket(socket_handle, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(socket_handle, F_GETFL, 0);
  if (flags < 0)
  {
    return 0;
  }
  return fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int sawit_service_send_text(SawitServiceClient* client, const char* text)
{
  SawitServiceNative* native_state = NULL;
  size_t length = 0U;

  if (client == NULL || text == NULL || !client->socket_open)
  {
    return 0;
  }

  native_state = (SawitServiceNative*)client->native_state;
  if (native_state == NULL || native_state->socket_handle == SAWIT_INVALID_SOCKET_HANDLE)
  {
    return 0;
  }

  length = strlen(text);
  if (length == 0U)
  {
    return 0;
  }

#if defined(_WIN32)
  return send(native_state->socket_handle, text, (int)length, 0) == (int)length;
#else
  return send(native_state->socket_handle, text, length, 0) == (ssize_t)length;
#endif
}

static void sawit_service_send_hello(SawitServiceClient* client)
{
  char packet[192] = { 0 };

  if (client == NULL)
  {
    return;
  }

  (void)snprintf(
    packet,
    sizeof(packet),
    "HELLO %u %llu %s\n",
    (unsigned int)k_sawit_protocol_version,
    (unsigned long long)client->room_id,
    client->player_name);
  (void)sawit_service_send_text(client, packet);
}

static void sawit_service_send_disconnect(SawitServiceClient* client)
{
  if (client == NULL || !client->socket_open)
  {
    return;
  }

  (void)sawit_service_send_text(client, "DISCONNECT\n");
}

static void sawit_service_send_input(
  SawitServiceClient* client,
  const PlayerController* player,
  const PlatformInput* input,
  unsigned int dt_ms
)
{
  char packet[192] = { 0 };
  float movement_x = 0.0f;
  float movement_z = 0.0f;
  float movement_length = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;

  if (client == NULL || player == NULL || input == NULL)
  {
    return;
  }

  movement_x = input->move_right;
  movement_z = input->move_forward;
  movement_length = sqrtf(movement_x * movement_x + movement_z * movement_z);
  if (movement_length > 1.0f)
  {
    movement_x /= movement_length;
    movement_z /= movement_length;
  }

  yaw = player->camera.yaw;
  pitch = player->camera.pitch;
  client->input_seq += 1U;
  client->client_tick += 1U;

  (void)snprintf(
    packet,
    sizeof(packet),
    "INPUT %u %u %u %.4f %.4f %.5f %.5f\n",
    client->input_seq,
    client->client_tick,
    dt_ms,
    movement_x,
    movement_z,
    yaw,
    pitch);
  (void)sawit_service_send_text(client, packet);
}

static void sawit_service_send_ping(SawitServiceClient* client)
{
  char packet[96] = { 0 };
  const uint64_t now_ms = sawit_service_now_ms();

  if (client == NULL)
  {
    return;
  }

  (void)snprintf(packet, sizeof(packet), "PING %llu\n", (unsigned long long)now_ms);
  (void)sawit_service_send_text(client, packet);
}

static void sawit_service_recv_packets(SawitServiceClient* client)
{
  SawitServiceNative* native_state = NULL;
  int packet_count = 0;

  if (client == NULL || !client->socket_open)
  {
    return;
  }

  native_state = (SawitServiceNative*)client->native_state;
  if (native_state == NULL || native_state->socket_handle == SAWIT_INVALID_SOCKET_HANDLE)
  {
    return;
  }

  for (packet_count = 0; packet_count < 32; ++packet_count)
  {
    char buffer[4096] = { 0 };
    int received = 0;

#if defined(_WIN32)
    received = recv(native_state->socket_handle, buffer, (int)sizeof(buffer) - 1, 0);
    if (received == SOCKET_ERROR)
    {
      const int error_code = WSAGetLastError();
      if (error_code == WSAEWOULDBLOCK || error_code == WSAECONNRESET)
      {
        return;
      }
      sawit_service_copy_text(client->last_error, sizeof(client->last_error), "recv failed");
      return;
    }
#else
    received = (int)recv(native_state->socket_handle, buffer, sizeof(buffer) - 1U, 0);
    if (received < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return;
      }
      sawit_service_copy_text(client->last_error, sizeof(client->last_error), "recv failed");
      return;
    }
#endif

    if (received <= 0)
    {
      return;
    }

    buffer[received] = '\0';
    sawit_service_parse_packet(client, buffer);
  }
}

static void sawit_service_parse_packet(SawitServiceClient* client, char* packet)
{
  if (client == NULL || packet == NULL)
  {
    return;
  }

  if (strncmp(packet, "WELCOME ", 8U) == 0)
  {
    sawit_service_parse_welcome(client, packet);
  }
  else if (strncmp(packet, "SNAPSHOT ", 9U) == 0)
  {
    sawit_service_parse_snapshot(client, packet);
  }
  else if (strncmp(packet, "PONG ", 5U) == 0)
  {
    sawit_service_parse_pong(client, packet);
  }
  else if (strncmp(packet, "ERROR ", 6U) == 0)
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), packet);
    diagnostics_logf("sawit_service: %s", packet);
  }
}

static void sawit_service_parse_welcome(SawitServiceClient* client, char* packet)
{
  char* cursor = packet;
  char* token = sawit_service_next_token(&cursor);
  unsigned int protocol_version = 0U;
  uint64_t player_id = 0U;
  uint64_t room_id = 0U;
  unsigned short tick_rate_hz = 0U;
  unsigned short snapshot_rate_hz = 0U;

  (void)token;
  if (!sawit_service_parse_u32(sawit_service_next_token(&cursor), &protocol_version) ||
    protocol_version != k_sawit_protocol_version ||
    !sawit_service_parse_u64(sawit_service_next_token(&cursor), &player_id) ||
    !sawit_service_parse_u64(sawit_service_next_token(&cursor), &room_id) ||
    !sawit_service_parse_u16(sawit_service_next_token(&cursor), &tick_rate_hz) ||
    !sawit_service_parse_u16(sawit_service_next_token(&cursor), &snapshot_rate_hz))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "bad WELCOME packet");
    return;
  }

  client->connected = 1;
  client->player_id = player_id;
  client->room_id = room_id;
  client->tick_rate_hz = tick_rate_hz;
  client->snapshot_rate_hz = snapshot_rate_hz;
  diagnostics_logf(
    "sawit_service: connected player_id=%llu room=%llu tick=%u snapshot=%u",
    (unsigned long long)client->player_id,
    (unsigned long long)client->room_id,
    (unsigned int)client->tick_rate_hz,
    (unsigned int)client->snapshot_rate_hz);
}

static void sawit_service_parse_snapshot(SawitServiceClient* client, char* packet)
{
  char* cursor = packet;
  char* token = sawit_service_next_token(&cursor);
  unsigned int tick = 0U;
  uint64_t room_id = 0U;
  unsigned int player_count = 0U;
  unsigned int player_index = 0U;
  int remote_count = 0;

  (void)token;
  if (!sawit_service_parse_u32(sawit_service_next_token(&cursor), &tick) ||
    !sawit_service_parse_u64(sawit_service_next_token(&cursor), &room_id) ||
    !sawit_service_parse_u32(sawit_service_next_token(&cursor), &player_count))
  {
    sawit_service_copy_text(client->last_error, sizeof(client->last_error), "bad SNAPSHOT header");
    return;
  }

  memset(client->remote_players, 0, sizeof(client->remote_players));
  client->remote_player_count = 0;

  for (player_index = 0U; player_index < player_count; ++player_index)
  {
    SawitRemotePlayer snapshot;
    unsigned int grounded = 0U;

    memset(&snapshot, 0, sizeof(snapshot));
    if (!sawit_service_parse_u64(sawit_service_next_token(&cursor), &snapshot.player_id) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.x) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.y) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.z) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.velocity_x) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.velocity_y) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.velocity_z) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.yaw) ||
      !sawit_service_parse_float(sawit_service_next_token(&cursor), &snapshot.pitch) ||
      !sawit_service_parse_u32(sawit_service_next_token(&cursor), &grounded) ||
      !sawit_service_parse_u32(sawit_service_next_token(&cursor), &snapshot.last_processed_input))
    {
      sawit_service_copy_text(client->last_error, sizeof(client->last_error), "bad SNAPSHOT player");
      return;
    }

    snapshot.grounded = grounded != 0U;
    snapshot.active = 1;
    if (snapshot.player_id == client->player_id)
    {
      client->local_authority = snapshot;
      client->local_authority_valid = 1;
    }
    else if (remote_count < SAWIT_SERVICE_MAX_REMOTE_PLAYERS)
    {
      client->remote_players[remote_count] = snapshot;
      remote_count += 1;
    }
  }

  client->last_snapshot_tick = tick;
  client->room_id = room_id;
  client->remote_player_count = remote_count;
}

static void sawit_service_parse_pong(SawitServiceClient* client, char* packet)
{
  char* cursor = packet;
  char* token = sawit_service_next_token(&cursor);
  uint64_t client_time_ms = 0U;
  uint64_t server_time_ms = 0U;
  const uint64_t now_ms = sawit_service_now_ms();

  (void)token;
  if (!sawit_service_parse_u64(sawit_service_next_token(&cursor), &client_time_ms) ||
    !sawit_service_parse_u64(sawit_service_next_token(&cursor), &server_time_ms))
  {
    return;
  }
  (void)server_time_ms;

  if (now_ms >= client_time_ms)
  {
    client->last_pong_rtt_ms = now_ms - client_time_ms;
  }
}

static char* sawit_service_next_token(char** cursor)
{
  char* start = NULL;

  if (cursor == NULL || *cursor == NULL)
  {
    return NULL;
  }

  start = *cursor;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
  {
    start += 1;
  }
  if (*start == '\0')
  {
    *cursor = start;
    return NULL;
  }

  *cursor = start;
  while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t' && **cursor != '\r' && **cursor != '\n')
  {
    *cursor += 1;
  }
  if (**cursor != '\0')
  {
    **cursor = '\0';
    *cursor += 1;
  }

  return start;
}

static int sawit_service_parse_u64(const char* text, uint64_t* out_value)
{
  char* end = NULL;
  unsigned long long value = 0U;

  if (text == NULL || out_value == NULL)
  {
    return 0;
  }

  errno = 0;
  value = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0')
  {
    return 0;
  }
  *out_value = (uint64_t)value;
  return 1;
}

static int sawit_service_parse_u32(const char* text, unsigned int* out_value)
{
  uint64_t value = 0U;
  if (!sawit_service_parse_u64(text, &value) || value > 0xffffffffULL || out_value == NULL)
  {
    return 0;
  }
  *out_value = (unsigned int)value;
  return 1;
}

static int sawit_service_parse_u16(const char* text, unsigned short* out_value)
{
  uint64_t value = 0U;
  if (!sawit_service_parse_u64(text, &value) || value > 0xffffULL || out_value == NULL)
  {
    return 0;
  }
  *out_value = (unsigned short)value;
  return 1;
}

static int sawit_service_parse_float(const char* text, float* out_value)
{
  char* end = NULL;
  float value = 0.0f;

  if (text == NULL || out_value == NULL)
  {
    return 0;
  }

  errno = 0;
  value = strtof(text, &end);
  if (errno != 0 || end == text || *end != '\0')
  {
    return 0;
  }
  *out_value = value;
  return 1;
}

static uint64_t sawit_service_now_ms(void)
{
#if defined(_WIN32)
  return (uint64_t)GetTickCount64();
#else
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
  {
    return 0U;
  }
  return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)(now.tv_nsec / 1000000L);
#endif
}
