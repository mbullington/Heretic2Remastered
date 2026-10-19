// net_dos.c -- DOS networking for the production engine: loopback for local
// play plus external IPX (Novell INT 2Fh API) when an IPX driver is present.
// DOSBox Staging provides that API itself with ipx=true (ipxnet server or
// tunnel); on real hardware the same calls run against a loaded packet/IPX
// driver (IPX.COM + NIC driver). Without IPX the backend stays loopback-only.

#include "qcommon.h"
#include "Random.h"

#include <dpmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>

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


static unsigned NET_TimeMS(void);

// ---------------------------------------------------------------------------
// IPX transport (INT 2Fh, AX=7A00h entry point; ECB-based datagrams).
// All ECBs, headers, and payloads live in one DPMI DOS-memory block; the
// protected-mode side stages through dosmemput/dosmemget. Receive is polled
// from NET_GetPacket, matching the engine's non-blocking socket contract.
// ---------------------------------------------------------------------------
#define IPX_HDR_LEN 30
#define IPX_DATA_MAX 2048
#define IPX_ECB_LEN 48
#define IPX_RX_PER_SOCKET 2
#define IPX_DOS_BYTES 16384

typedef struct
{
	qboolean opened;
	unsigned short socket_host; // host-order socket number
	unsigned tx_base;           // DOS offsets: ECB, header, data
	unsigned rx_base[IPX_RX_PER_SOCKET];
} ipx_socket_t;

static qboolean ipx_available;
static unsigned ipx_entry_seg, ipx_entry_off;
static unsigned ipx_dos_seg;
static int ipx_dos_selector;
static byte ipx_local_net[4];
static byte ipx_local_node[6];
static ipx_socket_t ipx_sockets[NUM_LOOPBACK_SOCKETS];

#define IPX_OFF_STACKTOP 512
#define IPX_OFF_ADDRBUF 512
#define IPX_OFF_TARGET_IN 528
#define IPX_OFF_TARGET_OUT 544
#define IPX_OFF_SOCKETS 1024
#define IPX_SLOT_BYTES (IPX_ECB_LEN + IPX_HDR_LEN + IPX_DATA_MAX)

static unsigned short ipx_htons(const unsigned short v)
{
	return (unsigned short)((v << 8) | (v >> 8));
}

// Invoke the IPX far entry with BX=function; ES:SI / ES:DI preloaded by caller
// fields in regs (es, si, di, dx, al via h regs union).
static void ipx_invoke(__dpmi_regs* regs)
{
	regs->x.cs = (unsigned short)ipx_entry_seg;
	regs->x.ip = (unsigned short)ipx_entry_off;
	regs->x.ss = (unsigned short)ipx_dos_seg;
	regs->x.sp = IPX_OFF_STACKTOP - 16;
	regs->x.es = (unsigned short)ipx_dos_seg;
	__dpmi_simulate_real_mode_procedure_retf(regs);
}

static void ipx_write_ecb(const unsigned base, const unsigned short socket_host,
	const byte* immediate, const unsigned hdr_off, const unsigned data_off, const int data_len)
{
	byte ecb[IPX_ECB_LEN];
	memset(ecb, 0, sizeof(ecb));
	ecb[10] = (byte)(socket_host >> 8); // socket number, big-endian
	ecb[11] = (byte)(socket_host & 0xFF);
	if (immediate != NULL)
		memcpy(ecb + 28, immediate, 6);
	ecb[34] = 2; // fragmentCount (little-endian word)
	ecb[35] = 0;
	// Fragment 0: IPX header; fragment 1: payload. Far pointers are stored
	// offset-first, then segment, each little-endian.
	ecb[36] = (byte)(hdr_off & 0xFF);
	ecb[37] = (byte)((hdr_off >> 8) & 0xFF);
	ecb[38] = (byte)(ipx_dos_seg & 0xFF);
	ecb[39] = (byte)((ipx_dos_seg >> 8) & 0xFF);
	ecb[40] = (byte)(IPX_HDR_LEN & 0xFF);
	ecb[41] = (byte)(IPX_HDR_LEN >> 8);
	ecb[42] = (byte)(data_off & 0xFF);
	ecb[43] = (byte)((data_off >> 8) & 0xFF);
	ecb[44] = (byte)(ipx_dos_seg & 0xFF);
	ecb[45] = (byte)((ipx_dos_seg >> 8) & 0xFF);
	ecb[46] = (byte)(data_len & 0xFF);
	ecb[47] = (byte)((data_len >> 8) & 0xFF);
	dosmemput(ecb, sizeof(ecb), (unsigned long)ipx_dos_seg * 16ul + base);
}

static byte ipx_dos_peek(const unsigned offset)
{
	byte value;
	dosmemget((unsigned long)ipx_dos_seg * 16ul + offset, 1, &value);
	return value;
}

static qboolean ipx_open_socket(ipx_socket_t* sock)
{
	__dpmi_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.x.bx = 0x0000; // OpenSocket
	regs.h.al = 0;      // short-lived socket
	regs.x.dx = ipx_htons(sock->socket_host);
	ipx_invoke(&regs);
	if (regs.h.al != 0)
		return false;
	sock->opened = true;
	return true;
}

static void ipx_post_listen(const ipx_socket_t* sock, const unsigned base)
{
	const unsigned hdr_off = base + IPX_ECB_LEN;
	const unsigned data_off = hdr_off + IPX_HDR_LEN;
	ipx_write_ecb(base, sock->socket_host, NULL, hdr_off, data_off, IPX_DATA_MAX);
	__dpmi_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.x.bx = 0x0004; // ListenForPacket
	regs.x.si = (unsigned short)base;
	ipx_invoke(&regs);
}

static const byte* ipx_immediate(const byte* dest_net, const byte* dest_node)
{
	static struct { byte net[4]; byte node[6]; byte imm[6]; } cache[16];
	static int cache_next;
	static byte fallback[6];
	qboolean broadcast = true;
	for (int i = 0; i < 6; ++i)
		if (dest_node[i] != 0xFF)
			broadcast = false;
	if (broadcast)
	{
		memset(fallback, 0xFF, 6);
		return fallback;
	}
	for (int i = 0; i < 16; ++i)
		if (memcmp(cache[i].net, dest_net, 4) == 0 && memcmp(cache[i].node, dest_node, 6) == 0 &&
			(cache[i].imm[0] | cache[i].imm[1] | cache[i].imm[2] | cache[i].imm[3] | cache[i].imm[4] | cache[i].imm[5]) != 0)
			return cache[i].imm;
	// GetLocalTarget: ES:SI = destination (net+node), ES:DI = immediate out.
	byte dest[10];
	memcpy(dest, dest_net, 4);
	memcpy(dest + 4, dest_node, 6);
	dosmemput(dest, sizeof(dest), (unsigned long)ipx_dos_seg * 16ul + IPX_OFF_TARGET_IN);
	__dpmi_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.x.bx = 0x0002;
	regs.x.si = IPX_OFF_TARGET_IN;
	regs.x.di = IPX_OFF_TARGET_OUT;
	ipx_invoke(&regs);
	byte imm[6];
	if (regs.h.al != 0)
		memcpy(imm, dest_node, 6); // same-segment assumption
	else
		dosmemget((unsigned long)ipx_dos_seg * 16ul + IPX_OFF_TARGET_OUT, 6, imm);
	memcpy(cache[cache_next].net, dest_net, 4);
	memcpy(cache[cache_next].node, dest_node, 6);
	memcpy(cache[cache_next].imm, imm, 6);
	const byte* result = cache[cache_next].imm;
	cache_next = (cache_next + 1) & 15;
	return result;
}

static qboolean ipx_send(const netsrc_t sock, const int length, const void* data, const netadr_t* to)
{
	ipx_socket_t* s = &ipx_sockets[sock];
	if (!s->opened || length < 0 || length > IPX_DATA_MAX)
		return false;
	byte dest_net[4];
	byte dest_node[6];
	unsigned short dest_sock_be;
	if (to->type == NA_BROADCAST_IPX)
	{
		memset(dest_net, 0, 4);
		memset(dest_node, 0xFF, 6);
	}
	else
	{
		memcpy(dest_net, to->ipx, 4);
		memcpy(dest_node, to->ipx + 4, 6);
	}
	// netadr.port is stored big-endian (Winsock convention in net_wins.c).
	dest_sock_be = to->port;
	const unsigned hdr_off = s->tx_base + IPX_ECB_LEN;
	const unsigned data_off = hdr_off + IPX_HDR_LEN;
	byte hdr[IPX_HDR_LEN];
	memset(hdr, 0, sizeof(hdr));
	hdr[0] = 0xFF; hdr[1] = 0xFF; // checksum unused
	const int total = IPX_HDR_LEN + length;
	hdr[2] = (byte)(total >> 8);
	hdr[3] = (byte)(total & 0xFF);
	hdr[4] = 0; // transport control
	hdr[5] = 4; // packet exchange
	memcpy(hdr + 6, dest_net, 4);
	memcpy(hdr + 10, dest_node, 6);
	hdr[16] = (byte)(dest_sock_be & 0xFF);
	hdr[17] = (byte)((dest_sock_be >> 8) & 0xFF);
	memcpy(hdr + 18, ipx_local_net, 4);
	memcpy(hdr + 22, ipx_local_node, 6);
	hdr[28] = (byte)(s->socket_host >> 8);
	hdr[29] = (byte)(s->socket_host & 0xFF);
	const unsigned long dos_base = (unsigned long)ipx_dos_seg * 16ul;
	dosmemput(hdr, sizeof(hdr), dos_base + hdr_off);
	if (length > 0)
		dosmemput(data, (size_t)length, dos_base + data_off);
	ipx_write_ecb(s->tx_base, s->socket_host, ipx_immediate(dest_net, dest_node), hdr_off, data_off, length);
	__dpmi_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.x.bx = 0x0003; // SendPacket
	regs.x.si = (unsigned short)s->tx_base;
	ipx_invoke(&regs);
	const unsigned deadline = NET_TimeMS() + 50;
	while (ipx_dos_peek(s->tx_base + 8) != 0) // inUseFlag
		if (NET_TimeMS() >= deadline)
			return false;
	return ipx_dos_peek(s->tx_base + 9) == 0; // completionCode
}

static qboolean ipx_receive(const netsrc_t sock, netadr_t* from, sizebuf_t* message)
{
	ipx_socket_t* s = &ipx_sockets[sock];
	if (!s->opened)
		return false;
	const unsigned long dos_base = (unsigned long)ipx_dos_seg * 16ul;
	for (int r = 0; r < IPX_RX_PER_SOCKET; ++r)
	{
		const unsigned base = s->rx_base[r];
		if (ipx_dos_peek(base + 8) != 0)
			continue; // still listening
		const byte completion = ipx_dos_peek(base + 9);
		byte hdr[IPX_HDR_LEN];
		dosmemget(dos_base + base + IPX_ECB_LEN, sizeof(hdr), hdr);
		const int total = (hdr[2] << 8) | hdr[3];
		const int payload = total - IPX_HDR_LEN;
		qboolean ok = false;
		if (completion == 0 && payload >= 0 && payload <= IPX_DATA_MAX && payload <= message->maxsize)
		{
			if (payload > 0)
				dosmemget(dos_base + base + IPX_ECB_LEN + IPX_HDR_LEN, (size_t)payload, message->data);
			message->cursize = payload;
			memset(from, 0, sizeof(*from));
			from->type = NA_IPX;
			memcpy(from->ipx, hdr + 18, 4);
			memcpy(from->ipx + 4, hdr + 22, 6);
			from->port = (unsigned short)(hdr[28] | (hdr[29] << 8));
			ok = true;
		}
		ipx_post_listen(s, base);
		if (ok)
		{
			packets_received++;
			return true;
		}
		packets_dropped++;
	}
	return false;
}

static qboolean ipx_init(void)
{
	__dpmi_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.x.ax = 0x7A00;
	__dpmi_simulate_real_mode_interrupt(0x2F, &regs);
	if (regs.h.al != 0xFF)
		return false;
	ipx_entry_seg = regs.x.es;
	ipx_entry_off = regs.x.di;
	const int seg = __dpmi_allocate_dos_memory(IPX_DOS_BYTES / 16, &ipx_dos_selector);
	if (seg < 0)
		return false;
	ipx_dos_seg = (unsigned)seg;
	// Local internetwork address.
	memset(&regs, 0, sizeof(regs));
	regs.x.bx = 0x0009;
	regs.x.si = IPX_OFF_ADDRBUF;
	ipx_invoke(&regs);
	byte addr[10];
	dosmemget((unsigned long)ipx_dos_seg * 16ul + IPX_OFF_ADDRBUF, sizeof(addr), addr);
	memcpy(ipx_local_net, addr, 4);
	memcpy(ipx_local_node, addr + 4, 6);
	const int server_port = (int)Cvar_Get("port", va("%i", PORT_SERVER), CVAR_NOSET)->value;
	const int client_port = (int)Cvar_Get("ipx_clientport", "0", CVAR_NOSET)->value != 0 ?
		(int)Cvar_Get("ipx_clientport", "0", CVAR_NOSET)->value :
		(int)Cvar_Get("clientport", va("%i", PORT_CLIENT), CVAR_NOSET)->value;
	unsigned offset = IPX_OFF_SOCKETS;
	for (int i = 0; i < NUM_LOOPBACK_SOCKETS; ++i)
	{
		ipx_sockets[i].socket_host = (unsigned short)(i == NS_SERVER ? server_port : client_port);
		ipx_sockets[i].tx_base = offset; offset += IPX_SLOT_BYTES;
		for (int r = 0; r < IPX_RX_PER_SOCKET; ++r)
		{
			ipx_sockets[i].rx_base[r] = offset; offset += IPX_SLOT_BYTES;
		}
		if (!ipx_open_socket(&ipx_sockets[i]))
			return false;
		for (int r = 0; r < IPX_RX_PER_SOCKET; ++r)
			ipx_post_listen(&ipx_sockets[i], ipx_sockets[i].rx_base[r]);
	}
	ipx_available = true;
	Com_Printf("DOS IPX initialized: %02x%02x%02x%02x:%02x%02x%02x%02x%02x%02x (client %i, server %i)\n",
		ipx_local_net[0], ipx_local_net[1], ipx_local_net[2], ipx_local_net[3],
		ipx_local_node[0], ipx_local_node[1], ipx_local_node[2], ipx_local_node[3], ipx_local_node[4], ipx_local_node[5],
		client_port, server_port);
	return true;
}

static void ipx_shutdown(void)
{
	if (!ipx_available)
		return;
	for (int i = 0; i < NUM_LOOPBACK_SOCKETS; ++i)
	{
		if (ipx_sockets[i].opened)
		{
			__dpmi_regs regs;
			memset(&regs, 0, sizeof(regs));
			regs.x.bx = 0x0001; // CloseSocket
			regs.x.dx = ipx_htons(ipx_sockets[i].socket_host);
			ipx_invoke(&regs);
			ipx_sockets[i].opened = false;
		}
	}
	__dpmi_free_dos_memory(ipx_dos_selector);
	ipx_available = false;
}

static int ipx_hexvalue(const char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

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
			address->ip[0], address->ip[1], address->ip[2], address->ip[3], (unsigned)ipx_htons(address->port));
		return text;
	}
	if (address->type == NA_IPX || address->type == NA_BROADCAST_IPX)
	{
		Com_sprintf(text, sizeof(text), "%02x%02x%02x%02x:%02x%02x%02x%02x%02x%02x:%u",
			address->ipx[0], address->ipx[1], address->ipx[2], address->ipx[3],
			address->ipx[4], address->ipx[5], address->ipx[6], address->ipx[7], address->ipx[8], address->ipx[9],
			(unsigned)ipx_htons(address->port));
		return text;
	}
	Com_sprintf(text, sizeof(text), "unsupported:%d", address->type);
	return text;
}

qboolean NET_StringToAdr(const char* text, netadr_t* address)
{
	if (text == NULL || address == NULL)
		return false;
	if (Q_stricmp(text, "localhost") == 0 || Q_stricmp(text, "loopback") == 0)
	{
		memset(address, 0, sizeof(*address));
		address->type = NA_LOOPBACK;
		return true;
	}
	// IPX form "xxxxxxxx:xxxxxxxxxxxx:port" (network:node:socket),
	// matching net_wins.c so connect strings are portable across builds.
	if (strlen(text) >= 23 && text[8] == ':' && text[21] == ':')
	{
		byte parsed[10];
		for (int i = 0; i < 10; ++i)
		{
			const char* pair = text + (i < 4 ? i * 2 : 9 + (i - 4) * 2);
			const int hi = ipx_hexvalue(pair[0]);
			const int lo = ipx_hexvalue(pair[1]);
			if (hi < 0 || lo < 0)
				return false;
			parsed[i] = (byte)((hi << 4) | lo);
		}
		memset(address, 0, sizeof(*address));
		address->type = NA_IPX;
		memcpy(address->ipx, parsed, sizeof(parsed));
		address->port = ipx_htons((unsigned short)Q_atoi(text + 22));
		return true;
	}
	return false;
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
		return ipx_available && ipx_receive(sock, from, message);
	}

	loopback_t* loop = &loopbacks[sock];
	if (loop->send - loop->get > MAX_LOOPBACK)
	{
		packets_dropped += loop->send - loop->get - MAX_LOOPBACK;
		loop->get = loop->send - MAX_LOOPBACK;
	}
	if (loop->get >= loop->send)
		return ipx_available && ipx_receive(sock, from, message);

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
	if (to->type == NA_IPX || to->type == NA_BROADCAST_IPX)
	{
		if (ipx_available && ipx_send(sock, length, data, to))
			packets_sent++;
		else
			packets_dropped++;
		return;
	}
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
	if (!ipx_init())
		Com_Printf("DOS loopback networking initialized (no IPX driver found)\n");
}

void NET_Shutdown(void)
{
	ipx_shutdown();
	Com_Printf("DOS network packets: sent=%u received=%u dropped=%u\n",
		packets_sent, packets_received, packets_dropped);
	memset(loopbacks, 0, sizeof(loopbacks));
	memset(delayed, 0, sizeof(delayed));
}
