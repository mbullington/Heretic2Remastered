// net_dos.c -- loopback-only networking for the DOS production engine.

#include "qcommon.h"
#include "Random.h"

#include <stdio.h>
#include <string.h>

#define MAX_LOOPBACK 4
#define NUM_LOOPBACK_SOCKETS 2
#define NUM_DELAYED_MESSAGES 20

typedef struct
{
	byte data[MAX_MSGLEN];
	int length;
} loop_message_t;

typedef struct
{
	loop_message_t messages[MAX_LOOPBACK];
	unsigned get;
	unsigned send;
} loopback_t;

typedef struct
{
	byte data[MAX_MSGLEN];
	int length;
	unsigned deliver_at;
	qboolean occupied;
} delayed_message_t;

static loopback_t loopbacks[NUM_LOOPBACK_SOCKETS];
static delayed_message_t delayed[NUM_LOOPBACK_SOCKETS][NUM_DELAYED_MESSAGES];
static unsigned packets_sent;
static unsigned packets_received;
static unsigned packets_dropped;

static unsigned NET_TimeMS(void)
{
	return (unsigned)(Sys_Microseconds() / 1000ll);
}

static qboolean NET_ValidSocket(const netsrc_t sock)
{
	return sock == NS_CLIENT || sock == NS_SERVER;
}

qboolean NET_CompareAdr(const netadr_t* a, const netadr_t* b)
{
	if (a == NULL || b == NULL || a->type != b->type)
		return false;

	if (a->type == NA_LOOPBACK)
		return true;
	if (a->type == NA_IP)
		return a->port == b->port && memcmp(a->ip, b->ip, sizeof(a->ip)) == 0;
	if (a->type == NA_IPX)
		return a->port == b->port && memcmp(a->ipx, b->ipx, sizeof(a->ipx)) == 0;
	return false;
}

qboolean NET_CompareBaseAdr(const netadr_t* a, const netadr_t* b)
{
	if (a == NULL || b == NULL || a->type != b->type)
		return false;

	if (a->type == NA_LOOPBACK)
		return true;
	if (a->type == NA_IP)
		return memcmp(a->ip, b->ip, sizeof(a->ip)) == 0;
	if (a->type == NA_IPX)
		return memcmp(a->ipx, b->ipx, sizeof(a->ipx)) == 0;
	return false;
}

qboolean NET_IsLocalAddress(const netadr_t* address)
{
	return address != NULL && address->type == NA_LOOPBACK;
}

char* NET_AdrToString(const netadr_t* address)
{
	static char text[64];

	if (address == NULL)
		return "<null>";
	if (address->type == NA_LOOPBACK)
		return "loopback";
	if (address->type == NA_IP)
	{
		Com_sprintf(text, sizeof(text), "%u.%u.%u.%u:%u",
			address->ip[0], address->ip[1], address->ip[2], address->ip[3], address->port);
		return text;
	}
	Com_sprintf(text, sizeof(text), "unsupported:%d", address->type);
	return text;
}

qboolean NET_StringToAdr(const char* text, netadr_t* address)
{
	if (text == NULL || address == NULL)
		return false;
	if (Q_stricmp(text, "localhost") != 0 && Q_stricmp(text, "loopback") != 0)
		return false;

	memset(address, 0, sizeof(*address));
	address->type = NA_LOOPBACK;
	return true;
}

static qboolean NET_CopyPacket(const byte* data, const int length, netadr_t* from, sizebuf_t* message)
{
	if (length < 0 || length > message->maxsize)
	{
		packets_dropped++;
		return false;
	}

	memcpy(message->data, data, (size_t)length);
	message->cursize = length;
	memset(from, 0, sizeof(*from));
	from->type = NA_LOOPBACK;
	packets_received++;
	return true;
}

qboolean NET_GetPacket(const netsrc_t sock, netadr_t* from, sizebuf_t* message)
{
	if (!NET_ValidSocket(sock) || from == NULL || message == NULL || message->data == NULL)
		return false;

	if (net_latency != NULL && net_latency->value > 0.0f && net_latency->value < 2000.0f)
	{
		const unsigned now = NET_TimeMS();
		for (int i = 0; i < NUM_DELAYED_MESSAGES; ++i)
		{
			delayed_message_t* packet = &delayed[sock][i];
			if (packet->occupied && packet->deliver_at <= now)
			{
				packet->occupied = false;
				if (!NET_CopyPacket(packet->data, packet->length, from, message))
					return false;
				if (net_receiverate->value > 0.0f && net_receiverate->value < 1.0f &&
					flrand(0.0f, 1.0f) > net_receiverate->value)
				{
					message->cursize = 0;
					packets_dropped++;
					return false;
				}
				return true;
			}
		}
		return false;
	}

	loopback_t* loop = &loopbacks[sock];
	if (loop->send - loop->get > MAX_LOOPBACK)
	{
		packets_dropped += loop->send - loop->get - MAX_LOOPBACK;
		loop->get = loop->send - MAX_LOOPBACK;
	}
	if (loop->get >= loop->send)
		return false;

	loop_message_t* packet = &loop->messages[loop->get & (MAX_LOOPBACK - 1)];
	loop->get++;
	if (!NET_CopyPacket(packet->data, packet->length, from, message))
		return false;
	if (net_receiverate->value > 0.0f && net_receiverate->value < 1.0f &&
		flrand(0.0f, 1.0f) > net_receiverate->value)
	{
		message->cursize = 0;
		packets_dropped++;
		return false;
	}
	return true;
}

void NET_SendPacket(const netsrc_t sock, const int length, const void* data, const netadr_t* to)
{
	if (!NET_ValidSocket(sock) || data == NULL || to == NULL)
		return;
	if (to->type != NA_LOOPBACK)
	{
		Com_DPrintf("DOS networking rejected non-loopback address %s\n", NET_AdrToString(to));
		packets_dropped++;
		return;
	}
	if (length < 0 || length > MAX_MSGLEN)
	{
		Com_Error(ERR_DROP, "NET_SendPacket: invalid loopback packet length %d", length);
		return;
	}
	if (net_sendrate->value > 0.0f && net_sendrate->value < 1.0f &&
		net_sendrate->value < flrand(0.0f, 1.0f))
	{
		packets_dropped++;
		return;
	}

	const netsrc_t destination = sock == NS_CLIENT ? NS_SERVER : NS_CLIENT;
	if (net_latency != NULL && net_latency->value > 0.0f && net_latency->value < 2000.0f)
	{
		for (int i = 0; i < NUM_DELAYED_MESSAGES; ++i)
		{
			delayed_message_t* packet = &delayed[destination][i];
			if (!packet->occupied)
			{
				const float latency = net_latency->value;
				packet->occupied = true;
				packet->deliver_at = NET_TimeMS() + (unsigned)(latency + flrand(-latency * 0.25f, latency * 0.25f));
				packet->length = length;
				memcpy(packet->data, data, (size_t)length);
				packets_sent++;
				return;
			}
		}
		packets_dropped++;
		return;
	}

	loopback_t* loop = &loopbacks[destination];
	loop_message_t* packet = &loop->messages[loop->send & (MAX_LOOPBACK - 1)];
	loop->send++;
	packet->length = length;
	memcpy(packet->data, data, (size_t)length);
	packets_sent++;
}

void NET_Config(const qboolean multiplayer)
{
	(void)multiplayer;
}

h2dos_net_stats_t NET_GetDOSStats(void)
{
	const h2dos_net_stats_t stats = {
		.packets_sent = packets_sent,
		.packets_received = packets_received,
		.packets_dropped = packets_dropped,
	};
	return stats;
}

void NET_Init(void)
{
	memset(loopbacks, 0, sizeof(loopbacks));
	memset(delayed, 0, sizeof(delayed));
	packets_sent = 0;
	packets_received = 0;
	packets_dropped = 0;
	Com_Printf("DOS loopback networking initialized (external networking disabled)\n");
}

void NET_Shutdown(void)
{
	Com_Printf("DOS loopback packets: sent=%u received=%u dropped=%u\n",
		packets_sent, packets_received, packets_dropped);
	memset(loopbacks, 0, sizeof(loopbacks));
	memset(delayed, 0, sizeof(delayed));
}
