/* The conversions between the host and the network byte order (the word of
 * Stackette is little endian, the network is big endian), and the stub of
 * get_bp() of inet's stack trace.
 */
unsigned short htons(unsigned short x)
{
	return (x >> 8) | (x << 8);
}

unsigned short ntohs(unsigned short x)
{
	return (x >> 8) | (x << 8);
}

unsigned long htonl(unsigned long x)
{
	return (x >> 24) | ((x >> 8) & 0xff00L) | ((x << 8) & 0xff0000L)
		| (x << 24);
}

unsigned long ntohl(unsigned long x)
{
	return (x >> 24) | ((x >> 8) & 0xff00L) | ((x << 8) & 0xff0000L)
		| (x << 24);
}

/* There is no frame pointer chain to walk for the stack trace. */
int get_bp(void)
{
	return 0;
}
