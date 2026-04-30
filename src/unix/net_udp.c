// net_udp.c -- POSIX UDP networking for macOS/Linux.

#include "qcommon.h"
#include "Random.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LOOPBACK	4
#define NUM_SOCKETS		3
#define NUM_LOOPMESSAGES 20
#define INVALID_SOCKET	(-1)

typedef int socket_t;

typedef struct
{
	byte data[MAX_MSGLEN];
	int datalen;
} loopmsg_t;

typedef struct
{
	byte data[MAX_MSGLEN];
	int datalen;
	uint timestamp;
	qboolean is_free;
} loopmsg2_t;

typedef struct
{
	loopmsg_t msgs[MAX_LOOPBACK];
	int get;
	int send;
} loopback_t;

static loopback_t loopbacks[NUM_SOCKETS];
static loopmsg2_t loopmessages[NUM_SOCKETS][NUM_LOOPMESSAGES];
static socket_t ip_sockets[NUM_SOCKETS] = { INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET };

static cvar_t* net_shownet;
static cvar_t* noudp;
static cvar_t* noipx;
static cvar_t* ipxfix;

static uint NET_TimeMS(void)
{
	return (uint)(Sys_Microseconds() / 1000ll);
}

static void NetadrToSockadr(const netadr_t* a, struct sockaddr_in* s)
{
	memset(s, 0, sizeof(*s));
	s->sin_family = AF_INET;
	s->sin_port = a->port;

	switch (a->type)
	{
		case NA_BROADCAST:
			s->sin_addr.s_addr = INADDR_BROADCAST;
			break;
		case NA_IP:
			memcpy(&s->sin_addr.s_addr, a->ip, sizeof(s->sin_addr.s_addr));
			break;
		default:
			break;
	}
}

static void SockadrToNetadr(const struct sockaddr_in* s, netadr_t* a)
{
	a->type = NA_IP;
	memcpy(a->ip, &s->sin_addr.s_addr, sizeof(s->sin_addr.s_addr));
	a->port = s->sin_port;
}

qboolean NET_CompareAdr(const netadr_t* a, const netadr_t* b)
{
	if (a->type != b->type)
		return false;

	switch (a->type)
	{
		case NA_LOOPBACK:
			return true;
		case NA_IP:
			return (a->port == b->port && memcmp(a->ip, b->ip, 4) == 0);
		case NA_IPX:
			return (a->port == b->port && memcmp(a->ipx, b->ipx, 10) == 0);
		default:
			return false;
	}
}

qboolean NET_CompareBaseAdr(const netadr_t* a, const netadr_t* b)
{
	if (a->type != b->type)
		return false;

	switch (a->type)
	{
		case NA_LOOPBACK:
			return true;
		case NA_IP:
			return (memcmp(a->ip, b->ip, 4) == 0);
		case NA_IPX:
			return (memcmp(a->ipx, b->ipx, 10) == 0);
		default:
			return false;
	}
}

static qboolean NET_StringToSockaddr(const char* s, struct sockaddr_in* sadr)
{
	memset(sadr, 0, sizeof(*sadr));
	sadr->sin_family = AF_INET;
	sadr->sin_port = 0;

	char copy[128];
	strcpy_s(copy, sizeof(copy), s);

	char* colon = strrchr(copy, ':');
	if (colon != NULL)
	{
		*colon = '\0';
		sadr->sin_port = htons((ushort)Q_atoi(colon + 1));
	}

	if (copy[0] == '\0' || Q_stricmp(copy, "localhost") == 0)
	{
		sadr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return true;
	}

	if (inet_pton(AF_INET, copy, &sadr->sin_addr) == 1)
		return true;

	struct hostent* h = gethostbyname(copy);
	if (h == NULL || h->h_addr_list == NULL || h->h_addr_list[0] == NULL)
		return false;

	memcpy(&sadr->sin_addr, h->h_addr_list[0], sizeof(sadr->sin_addr));
	return true;
}

char* NET_AdrToString(const netadr_t* a)
{
	static char s[64];

	switch (a->type)
	{
		case NA_LOOPBACK:
			Com_sprintf(s, sizeof(s), "loopback");
			break;
		case NA_IP:
			Com_sprintf(s, sizeof(s), "%i.%i.%i.%i:%i", a->ip[0], a->ip[1], a->ip[2], a->ip[3], ntohs(a->port));
			break;
		default:
			Com_sprintf(s, sizeof(s), "%02x%02x%02x%02x:%02x%02x%02x%02x%02x%02x:%i", a->ipx[0], a->ipx[1], a->ipx[2], a->ipx[3], a->ipx[4], a->ipx[5], a->ipx[6], a->ipx[7], a->ipx[8], a->ipx[9], ntohs(a->port));
			break;
	}

	return s;
}

qboolean NET_StringToAdr(const char* s, netadr_t* a)
{
	struct sockaddr_in sadr;

	if (strcmp(s, "localhost") == 0)
	{
		memset(a, 0, sizeof(netadr_t));
		a->type = NA_LOOPBACK;
		return true;
	}

	if (NET_StringToSockaddr(s, &sadr))
	{
		SockadrToNetadr(&sadr, a);
		return true;
	}

	return false;
}

static char* NET_ErrorString(void)
{
	return strerror(errno);
}

qboolean NET_IsLocalAddress(const netadr_t* a)
{
	return a->type == NA_LOOPBACK;
}

static qboolean NET_GetLoopPacket(const netsrc_t sock, netadr_t* n_from, sizebuf_t* n_message)
{
	if (net_latency->value > 0.0f && net_latency->value < 2000.0f)
	{
		const uint time = NET_TimeMS();
		loopmsg2_t* msg = &loopmessages[sock][0];

		for (int i = 0; i < NUM_LOOPMESSAGES; i++, msg++)
		{
			if (msg->is_free && msg->timestamp < time)
			{
				msg->is_free = false;
				memcpy(n_message->data, msg->data, msg->datalen);
				n_message->cursize = msg->datalen;
				memset(n_from, 0, sizeof(*n_from));
				n_from->type = NA_LOOPBACK;
				return true;
			}
		}

		return false;
	}

	loopback_t* loop = &loopbacks[sock];
	if (loop->send - loop->get > MAX_LOOPBACK)
		loop->get = loop->send - MAX_LOOPBACK;

	if (loop->get >= loop->send)
		return false;

	const int i = loop->get & (MAX_LOOPBACK - 1);
	loop->get++;

	memcpy(n_message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	n_message->cursize = loop->msgs[i].datalen;
	memset(n_from, 0, sizeof(*n_from));
	n_from->type = NA_LOOPBACK;
	return true;
}

static void NET_SendLoopPacket(const netsrc_t sock, const int length, const void* data)
{
	if (net_latency->value > 0.0f && net_latency->value < 2000.0f)
	{
		const uint time = NET_TimeMS();
		loopmsg2_t* msg = &loopmessages[sock ^ 1][0];

		for (int i = 0; i < NUM_LOOPMESSAGES; i++, msg++)
		{
			if (!msg->is_free)
			{
				msg->is_free = true;
				const float nl = net_latency->value;
				msg->timestamp = time + (int)(nl + flrand(-nl * 0.25f, nl * 0.25f));
				memcpy(msg->data, data, length);
				msg->datalen = length;
				return;
			}
		}

		return;
	}

	loopback_t* loop = &loopbacks[sock ^ 1];
	const int index = loop->send & (MAX_LOOPBACK - 1);
	loop->send++;

	memcpy(loop->msgs[index].data, data, length);
	loop->msgs[index].datalen = length;
}

qboolean NET_GetPacket(const netsrc_t sock, netadr_t* n_from, sizebuf_t* n_message)
{
	if (NET_GetLoopPacket(sock, n_from, n_message))
	{
		if (net_receiverate->value > 0.0f && net_receiverate->value < 1.0f && flrand(0.0f, 1.0f) > net_receiverate->value)
		{
			n_message->cursize = 0;
			return false;
		}

		return true;
	}

	const socket_t net_socket = ip_sockets[sock];
	if (net_socket == INVALID_SOCKET)
		return false;

	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	const int ret = (int)recvfrom(net_socket, n_message->data, n_message->maxsize, 0, (struct sockaddr*)&from, &fromlen);

	if (ret == -1)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			if ((int)dedicated->value)
				Com_Printf("NET_GetPacket: %s\n", NET_ErrorString());
			else
				Com_Error(ERR_DROP, "NET_GetPacket: %s", NET_ErrorString());
		}

		return false;
	}

	SockadrToNetadr(&from, n_from);
	if (ret == n_message->maxsize)
	{
		Com_Printf("Oversize packet from %s\n", NET_AdrToString(n_from));
		return false;
	}

	if (net_receiverate->value > 0.0f && net_receiverate->value < 1.0f && flrand(0.0f, 1.0f) > net_receiverate->value)
	{
		n_message->cursize = 0;
		return false;
	}

	n_message->cursize = ret;
	return true;
}

void NET_SendPacket(const netsrc_t sock, const int length, const void* data, const netadr_t* to)
{
	socket_t net_socket;
	struct sockaddr_in addr;

	if (net_sendrate->value > 0.0f && net_sendrate->value < 1.0f && net_sendrate->value < flrand(0.0f, 1.0f))
		return;

	switch (to->type)
	{
		case NA_LOOPBACK:
			NET_SendLoopPacket(sock, length, data);
			return;
		case NA_IP:
		case NA_BROADCAST:
			net_socket = ip_sockets[sock];
			break;
		case NA_IPX:
		case NA_BROADCAST_IPX:
			return;
		default:
			Com_Error(ERR_FATAL, "NET_SendPacket: bad address type");
			return;
	}

	if (net_socket == INVALID_SOCKET)
		return;

	NetadrToSockadr(to, &addr);
	if (sendto(net_socket, data, length, 0, (struct sockaddr*)&addr, sizeof(addr)) == -1)
	{
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
		if (errno == EADDRNOTAVAIL && to->type == NA_BROADCAST)
			return;

		if ((int)dedicated->value)
			Com_Printf("NET_SendPacket ERROR: %s\n", NET_ErrorString());
		else if (errno == EADDRNOTAVAIL)
			Com_DPrintf("NET_SendPacket Warning: %s : %s\n", NET_ErrorString(), NET_AdrToString(to));
		else
			Com_Error(ERR_DROP, "NET_SendPacket ERROR: %s\n", NET_ErrorString());
	}
}

static socket_t NET_IPSocket(const char* net_interface, const int port)
{
	int optval = 1;
	const socket_t newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (newsocket == INVALID_SOCKET)
	{
		Com_Printf("WARNING: UDP_OpenSocket: socket: %s\n", NET_ErrorString());
		return INVALID_SOCKET;
	}

	if (fcntl(newsocket, F_SETFL, O_NONBLOCK) == -1)
	{
		Com_Printf("WARNING: UDP_OpenSocket: fcntl O_NONBLOCK: %s\n", NET_ErrorString());
		close(newsocket);
		return INVALID_SOCKET;
	}

	if (setsockopt(newsocket, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) == -1)
	{
		Com_Printf("WARNING: UDP_OpenSocket: setsockopt SO_BROADCAST: %s\n", NET_ErrorString());
		close(newsocket);
		return INVALID_SOCKET;
	}

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;

	if (net_interface == NULL || net_interface[0] == 0 || Q_stricmp(net_interface, "localhost") == 0)
		address.sin_addr.s_addr = INADDR_ANY;
	else
		NET_StringToSockaddr(net_interface, &address);

	address.sin_port = (port == PORT_ANY) ? 0 : htons((ushort)port);

	if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == -1)
	{
		Com_Printf("WARNING: UDP_OpenSocket: bind: %s\n", NET_ErrorString());
		close(newsocket);
		return INVALID_SOCKET;
	}

	return newsocket;
}

static void NET_OpenIP(void)
{
	const cvar_t* ip = Cvar_Get("ip", "localhost", CVAR_NOSET);
	const qboolean is_dedicated = Cvar_IsSet("dedicated");

	if (ip_sockets[NS_SERVER] == INVALID_SOCKET)
	{
		int port = (int)Cvar_Get("ip_hostport", "0", CVAR_NOSET)->value;
		if (port == 0)
		{
			port = (int)Cvar_Get("hostport", "0", CVAR_NOSET)->value;
			if (port == 0)
				port = (int)Cvar_Get("port", va("%i", PORT_SERVER), CVAR_NOSET)->value;
		}

		ip_sockets[NS_SERVER] = NET_IPSocket(ip->string, port);
		if (is_dedicated && ip_sockets[NS_SERVER] == INVALID_SOCKET)
			Com_Error(ERR_FATAL, "Couldn't allocate dedicated server IP port");
	}

	if (is_dedicated)
		return;

	if (ip_sockets[NS_CLIENT] == INVALID_SOCKET)
	{
		int port = (int)Cvar_Get("ip_clientport", "0", CVAR_NOSET)->value;
		if (port == 0)
		{
			port = (int)Cvar_Get("clientport", va("%i", PORT_CLIENT), CVAR_NOSET)->value;
			if (port == 0)
				port = PORT_ANY;
		}

		ip_sockets[NS_CLIENT] = NET_IPSocket(ip->string, port);
		if (ip_sockets[NS_CLIENT] == INVALID_SOCKET)
			ip_sockets[NS_CLIENT] = NET_IPSocket(ip->string, PORT_ANY);
	}
}

void NET_Config(const qboolean multiplayer)
{
	static qboolean old_config;

	if (old_config == multiplayer)
		return;

	old_config = multiplayer;

	if (multiplayer)
	{
		if (!(int)noudp->value)
			NET_OpenIP();
	}
	else
	{
		for (int i = 0; i < NUM_SOCKETS; i++)
		{
			if (ip_sockets[i] != INVALID_SOCKET)
			{
				close(ip_sockets[i]);
				ip_sockets[i] = INVALID_SOCKET;
			}
		}
	}
}

void NET_Init(void)
{
	Com_Printf("UDP Initialized\n");

	noudp = Cvar_Get("noudp", "0", CVAR_NOSET);
	noipx = Cvar_Get("noipx", "1", CVAR_NOSET);
	ipxfix = Cvar_Get("ipxfix", "1", 0);
	net_shownet = Cvar_Get("net_shownet", "0", 0);
	(void)noipx;
	(void)ipxfix;
	(void)net_shownet;
}

void NET_Shutdown(void)
{
	NET_Config(false);
}
