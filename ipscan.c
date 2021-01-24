//    IPscan - an HTTP-initiated IPv6 port scanner.
//
//    Copyright (C) 2011-2021 Tim Chappell.
//
//    This file is part of IPscan.
//
//    IPscan is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with IPscan.  If not, see <http://www.gnu.org/licenses/>.

// ipscan.c version
// 0.01 - first released version
// 0.02 - additional DEBUG added for MySQL investigation
// 0.03 - added syslog support
// 0.04 - improved HTML (transition to styles, general compliance)
// 0.05 - addition of ICMPv6 ECHO-REQUEST functionality
// 0.06 - removal of empty HTML paragraph
// 0.07 - further buffer overflow prevention measures
// 0.08 - correct printf cast
// 0.09 - tidy up exit calls and verbosity support
// 0.10 - minor include correction for FreeBSD support
// 0.11 - add parallel port scan function
// 0.12 - remove unused parameters
// 0.13 - specifically count number of customport parameters
// 0.14 - add service names to results table (modification to portlist, now structure)
// 0.15 - fix length of requestmethod to prevent potential overflow
// 0.16 - add UDP port scan support
// 0.17 - add parallel UDP port scan support
// 0.18 - separate UDP and TCP debug logging
// 0.19 - added missing log prefix
// 0.20 - add scan automation help when offered a bad query string
// 0.21 - add support for removal of ping
// 0.22 - add support for removal of UDP
// 0.23 - add support for special test cases
// 0.24 - improve special test case debug logging
// 0.25 - add support for test completion reporting
// 0.25 - fix special case handling for custom ports
// 0.26 - correct fetch tidy-up reporting
// 0.27 - update to support further completion report types
// 0.28 - improved error logging
// 0.29 - use random(ish) sessions rather than getpid
// 0.30 - move to use strnlen() in getenv lookups
// 0.31 - improved querystring parsing, truncated session id
// 0.32 - add Navigate away detection
// 0.33 - add reporting for fork() issues
// 0.34 - add automated results deletion for javascript clients
// 0.35 - add support for deletion of orphaned results
// 0.36 - add time() response checks
// 0.37	- simplify reported syslog name
// 0.38 - remove exit() calls to simplify fuzzing
// 0.39 - transition to HTML5 support
// 0.40 - further HTML tag adjustments
// 0.41 - add TCP memcache port check
// 0.42 - logging in spirit of RFC6302 (default logging records IPv6 addresses as /48)
// 0.43 - exit from scan if terms were not accepted
// 0.44 - limited IPv6 address logging and further client debug
// 0.45 - further client debug improvements
// 0.46 - yet more client debug improvements
// 0.47 - yet more client debug improvements
// 0.48 - fix compilation on platforms which don't support UDP or SUID
// 0.49 - semmle re-entrant time function changes
// 0.50 - add page reload for case where terms and conditions not accepted
// 0.51 - further client debug improvements and copyright update
// 0.52 - remove summarise_db() functionality
// 0.53 - incorporate update_db() for test state
// 0.54 - insert delay before db_delete
// 0.55 - update logging for cases where database lookup returns UNKNOWN
// 0.56 - minor debug update to identify end-of-test client
// 0.57 - update copyright year and move to client session/starttime generation
// 0.58 - minor tweaks to delays before database record deletion at end of javascript test

#include "ipscan.h"
#include "ipscan_portlist.h"
//
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
// toupper/tolower routines
#include <ctype.h>

// Others that FreeBSD highlighted
#include <netinet/in.h>

// IPv6 address conversion
#include <arpa/inet.h>

// String comparison
#include <string.h>

// errors
#include <errno.h>

// Logging with syslog requires additional include
#if (1 == LOGMODE)
#include <syslog.h>
#endif

// Parallel processing related
#include <sys/wait.h>

//
// Prototype declarations
//

int write_db(uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session, uint32_t port, int32_t result, char *indirecthost);
int dump_db(uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session);
int read_db_result(uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session, uint32_t port);
int delete_from_db(uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session);
int tidy_up_db(uint64_t time_now);
int update_db(uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session, uint32_t port, int32_t result, char *indirecthost);

int check_udp_ports_parll(char * hostname, unsigned int portindex, unsigned int todo, uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session, struct portlist_struc *udpportlist);
int check_tcp_ports_parll(char * hostname, unsigned int portindex, unsigned int todo, uint64_t host_msb, uint64_t host_lsb, uint64_t timestamp, uint64_t session, struct portlist_struc *portlist);

void create_json_header(void);
void create_html_header(uint16_t numports, uint16_t numudpports, char * reconquery);
void create_html_body(char * hostname, time_t timestamp, uint16_t numports, uint16_t numudpports, struct portlist_struc *portlist, struct portlist_struc *udpportlist);

#ifdef IPSCAN_HTML5_ENABLED
void create_html5_common_header(void);
void create_html5_form(uint16_t numports, uint16_t numudpports, struct portlist_struc *portlist, struct portlist_struc *udpportlist);
#else
void create_html_form(uint16_t numports, uint16_t numudpports, struct portlist_struc *portlist, struct portlist_struc *udpportlist);
#endif

void create_html_common_header(void);
void create_html_body_end(void);

// from ipscan_general
#if (1 == TEXTMODE)
uint64_t get_session(void);
#endif
void proto_to_string(int proto, char * retstring);
void fetch_to_string(int fetchnum, char * retstring);
char * state_to_string(int statenum, char * retstringptr, int retstringfree);

// create_results_key_table is only referenced if creating the text-only version of the scanner
#if (1 == TEXTMODE)
void create_results_key_table(char * hostname, time_t timestamp);
#endif

// Only include reference to ping-test function if compiled in
#if (1 == IPSCAN_INCLUDE_PING)
int check_icmpv6_echoresponse(char * hostname, uint64_t starttime, uint64_t session, char * router);
#endif



//
// End of prototypes declarations
//

// structure holding the potential results table - entries MUST be in montonically increasing enumerated returnval order
struct rslt_struc resultsstruct[] =
{
		/* returnval,		connrc,	conn_errno		TEXT lbl			TEXT col	Description/User feedback	*/
		{ PORTOPEN, 		0, 		0,	 			"OPEN", 			"red",		"An IPv6 TCP connection was successfully established to this port. You should check that this is the expected outcome since an attacker may be able to compromise your machine by accessing this IPv6 address/port combination."},
		{ PORTABORT, 		-1, 	ECONNABORTED, 	"ABRT", 			"yellow",	"An abort indication was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTREFUSED, 		-1, 	ECONNREFUSED, 	"RFSD", 			"yellow",	"A refused indication (TCP RST/ACK or ICMPv6 type 1 code 4) was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTCRESET, 		-1, 	ECONNRESET, 	"CRST", 			"yellow",	"A connection reset request was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTNRESET, 		-1, 	ENETRESET, 		"NRST", 			"yellow",	"A network reset request was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTINPROGRESS, 	-1, 	EINPROGRESS, 	"STLTH", 			"green",	"No response was received from your machine in the allocated time period. This is the ideal response since no-one can ascertain your machines' presence at this IPv6 address/port combination."},
		{ PORTPROHIBITED, 	-1, 	EACCES, 		"PHBTD", 			"yellow",	"An administratively prohibited response (ICMPv6 type 1 code 1) was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTUNREACHABLE, 	-1, 	ENETUNREACH, 	"NUNRCH", 			"yellow",	"An unreachable response (ICMPv6 type 1 code 0) was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTNOROUTE, 		-1, 	EHOSTUNREACH, 	"HUNRCH", 			"yellow",	"A No route to host response (ICMPv6 type 1 code 3 or ICMPv6 type 3) was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTPKTTOOBIG, 	-1, 	EMSGSIZE, 		"TOOBIG", 			"yellow",	"A Packet too big response (ICMPv6 type 2) was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ PORTPARAMPROB, 	-1, 	EPROTO, 		"PRMPRB", 			"yellow",	"A Parameter problem response (ICMPv6 type 4) was received when attempting to open this port. Someone can ascertain that your machine is responding on this IPv6 address/port combination, but cannot establish a TCP connection."},
		{ ECHONOREPLY, 		-96, 	-96,	 		"ECHO NO REPLY",	"green",	"No ICMPv6 ECHO_REPLY packet was received in response to the ICMPv6 ECHO_REQUEST which was sent. This is the ideal response since no-one can ascertain your machines' presence at this IPv6 address."},
		{ ECHOREPLY, 		-97, 	-97,	 		"ECHO REPLY", 		"yellow",	"An ICMPv6 ECHO_REPLY packet was received in response to the ICMPv6 ECHO_REQUEST which was sent. Someone can ascertain that your machine is present on this IPv6 address."},
		{ UDPOPEN,			-95,	-95,			"UDPOPEN",			"red",		"A valid response was received from this UDP port. You should check that this is the expected outcome since an attacker may be able to compromise your machine by accessing this IPv6 address/port combination."},
		{ UDPSTEALTH,		-1,		EAGAIN,			"UDPSTEALTH",		"green",	"No UDP response was received from your machine in the allocated time period. This is the ideal response since no-one can ascertain your machines' presence at this IPv6 address/port combination."},
		/* Unexpected and unknown error response cases, do NOT change */
		{ PORTUNEXPECTED,	-98,	-98,			"UNXPCT",			"white",	"An unexpected response was received to the connect attempt."},
		{ PORTUNKNOWN, 		-99,	-99, 			"UNKWN", 			"white",	"An unknown error response was received, or the port is yet to be tested."},
		{ PORTINTERROR,		-100,	-100,			"INTERR",			"white",	"An internal error occurred."},
		/* End of list marker, do NOT change */
		{ PORTEOL,			-101,	-101,			"EOL",				"black",	"End of list marker."}
};

int main(void)
{

	#if (1 == TEXTMODE)
	// last is only used in text-only mode
	int last = 0;
	#else
	// fetchnum is only used in javascript-only mode
	int fetchnum = 0;
	#endif

	// List of ports to be tested and their results
	struct portlist_struc portlist[MAXPORTS];

	int result;

	#if (TEXTMODE != 1)
	// Default for unused database entries
	char unusedfield[] = "unused";
	#endif

	// Only necessary if we're including ping support
	#if (1 == IPSCAN_INCLUDE_PING)
	int pingresult;
	// Storage for indirecthost address, in case required
	char indirecthost[INET6_ADDRSTRLEN];
	#endif

	char remoteaddrstring[INET6_ADDRSTRLEN];
	char *remoteaddrvar;

	unsigned int position = 0;

	// Default to testing
	int beginscan = 0;
	int fetch = 0;

	// the session starttime, used as an unique index for the database
	time_t   starttime;
	// the query derived starttime
	int64_t  querystarttime;

	uint8_t special;
	uint16_t port;
	uint16_t portindex;

	// Parallel scanning related
	int numchildren;
	int remaining;
	int childstatus;
	unsigned int porti;

	// Ports to be tested
	uint16_t numports = 0;
	#if (1 == IPSCAN_INCLUDE_UDP)
	uint16_t numudpports = NUMUDPPORTS;
	#endif

	// "general purpose" variables, used as required
	int rc = 0;
	unsigned int i = 0;
	unsigned int shift = 0;
	unsigned int j = 0;

	// stats
	unsigned int portsstats[ NUMRESULTTYPES ];

	// Determine request method and query-string
	char requestmethod[ (MAXREQMETHODLEN + 1) ];
	char thischar;
	char *reqmethodvar;
	char *querystringvar;
	char querystring[ (MAXQUERYSTRLEN + 1) ];


	// buffer for reconstituted querystring
	size_t reconquerysize = MAXQUERYSTRLEN;
	char reconquery[ (MAXQUERYSTRLEN + 1) ];
	char *reconptr = &reconquery[0];

	// buffer for logging entries
	size_t logbuffersize = LOGENTRYLEN;
	char logbuffer[ (LOGENTRYLEN + 1) ];
	char *logbufferptr = &logbuffer[0];

	// Structure to hold querystring variable names, their values and a validity indication
	typedef struct {
		char varname[(MAXQUERYNAMELEN+2)];
		int64_t varval; // Signed since some values will be negative
		int valid;
	} queries;

	queries query[MAXQUERIES];
	unsigned int numqueries = 0;
	int64_t varval = 0; // temporary storage for query string parameters
	// value string - add two chars to cope with trailing \0
	char valstring[ (MAXQUERYVALLEN + 2) ];

	int includeexisting = 0;
	int termsaccepted = 0;

	// IPv6 address related
	unsigned char remotehost[sizeof(struct in6_addr)];

	uint64_t value;
	uint64_t remotehost_msb = 0ULL;
	uint64_t remotehost_lsb = 0ULL;

	// If syslog is in use then open the log
	#if (1 == LOGMODE)
	openlog(EXENAME, LOG_PID, LOG_LOCAL0);
	#endif

	// Initialise the port list
	for (i = 0; i < DEFNUMPORTS; i++)
	{
		portlist[i] = defportlist[i];
	}

	// Clear out the port result type statistics
	for (i = 0 ; i < NUMRESULTTYPES ; i++)
	{
		portsstats[i] = 0;
	}

	// Process id related - this version is extracted from the querystring
	// note: this is signed, whereas original value is a suitably truncated unsigned integer
	int64_t querysession = 0;

	// Log the current time and "session" with which to initiate scan and fetch results
	// These should ensure that each test is globally unique when client IP address is also used.
	starttime = time(NULL);
	if (starttime < 0)
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for starttime %d (%s)\n", errno, strerror(errno));
	}
	#if (1 == TEXTMODE)
	uint64_t session = get_session();
	#endif

	// QUERY_STRING / REQUEST_METHOD
	// URL is of the form: ipv6.cgi?name1=value1&name2=value2
	// REQUEST_METHOD = GET
	// QUERY_STRING = name1=value1&name2=value2
	reqmethodvar = getenv("REQUEST_METHOD");
	querystringvar = getenv("QUERY_STRING");

	// ensure length OK
	if (NULL == reqmethodvar)
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: ERROR : REQUEST_METHOD variable lookup returned NULL.");
	}
	else if ( strnlen(reqmethodvar, (MAXREQMETHODLEN+1)) > MAXREQMETHODLEN )
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: ATTACK?: REQUEST_METHOD variable string is longer than allocated buffer (%d > %d)\n", (int)strnlen(reqmethodvar, (MAXREQMETHODLEN+1)), MAXREQMETHODLEN);
		// Create the header
		HTML_HEADER();
		// Now finish the header
		printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
		printf("</head>\n");
		printf("<body>\n");
		printf("<p>I was called with REQUEST_METHOD longer than my allocated buffer. That is very disappointing.</p>\n");
		// Finish the html
		create_html_body_end();
		return(EXIT_SUCCESS);
	}
	else if( sscanf(reqmethodvar,"%"TO_STR(MAXREQMETHODLEN)"s",requestmethod) != 1 )
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: Invalid request-method scan.");
	}
	else
	{
		#ifdef QUERYDEBUG
		IPSCAN_LOG( LOGPREFIX "ipscan: Request method is : %s\n", requestmethod);
		#endif

		// Force Uppercase to ease comparison
		for (i = 0; i < (unsigned int)strnlen(requestmethod, (MAXREQMETHODLEN+1)); i++)
		{
			thischar=requestmethod[i];
			requestmethod[i]=(char)(toupper(thischar) &0xFF);
		}

		if (0 == strncmp("GET", requestmethod, 3))
		{
			if(NULL == querystringvar)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: QUERY_STRING variable lookup returned NULL.\n");
			}
			else if ( strnlen(querystringvar, MAXQUERYSTRLEN+1) > MAXQUERYSTRLEN)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ATTACK?: QUERY_STRING environment string is longer than allocated buffer (%d > %d)\n", (int)strnlen(querystringvar, MAXQUERYSTRLEN+1), MAXQUERYSTRLEN);
				// Create the header
				HTML_HEADER();
				// Now finish the header
				printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
				printf("</head>\n");
				printf("<body>\n");
				printf("<p>I was called with a QUERY_STRING longer than my allocated buffer. That is very disappointing.</p>\n");
				// Finish the html
				create_html_body_end();
				return(EXIT_SUCCESS);
			}
			else if( sscanf(querystringvar,"%"TO_STR(MAXQUERYSTRLEN)"s",querystring) != 1 )
			{
				#ifdef QUERYDEBUG
				// No query string will get reported here ....
				IPSCAN_LOG( LOGPREFIX "ipscan: Invalid query-string sscanf.\n");
				#endif
			}
			else
			{
				#ifdef QUERYDEBUG
				IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: Query-string : %s\n", querystring);
				#endif


				// Force lowercase to ease later comparison
				for (i = 0; i < (unsigned int)strnlen(querystring,(MAXQUERYSTRLEN)); i++)
				{
					thischar=querystring[i];
					querystring[i]=(char)(tolower(thischar) & 0xFF);
				}

				//
				// Split the query string into variable names and values
				//
				// URL is of the form: ipscan-js.cgi?name1=value1&name2=value2
				unsigned int queryindex = 0;
				int finished = 0;

				while (MAXQUERYSTRLEN > queryindex && 32 <= querystring[queryindex] && 0 == finished && MAXQUERIES > numqueries)
				{
					int varnameindex = 0;
					query[numqueries].valid = 0;
					while ( 32 <= querystring[queryindex] && 127 > querystring[queryindex] && '=' != querystring[queryindex] \
							&& '&' != querystring[queryindex] && MAXQUERYSTRLEN > queryindex && MAXQUERYNAMELEN > varnameindex && 0 == finished)
					{
						query[numqueries].varname[varnameindex] = querystring[queryindex];
						varnameindex ++;
						queryindex ++;
					}
					if (MAXQUERYNAMELEN <= varnameindex)
					{
						IPSCAN_LOG( LOGPREFIX "ipscan: query parameter name string is too long : %s\n", querystring);
						varnameindex = MAXQUERYNAMELEN; // Truncate
					}
					query[numqueries].varname[varnameindex]=0; // Add termination

					finished = (32 > querystring[queryindex] || 126 < querystring[queryindex] || MAXQUERYSTRLEN <= queryindex) ? 1 : 0;
					if (0 == finished && '=' == querystring[queryindex])
					{
						// Jump over '='
						while ('=' == querystring[queryindex] && MAXQUERYSTRLEN > queryindex)
						{
							queryindex++;
						}
						int valueindex = 0;
						while ( 32 <= querystring[queryindex] && 127 > querystring[queryindex] && '=' != querystring[queryindex] \
								&& '&' != querystring[queryindex] && MAXQUERYVALLEN > valueindex && MAXQUERYSTRLEN > queryindex)
						{
							valstring[valueindex] = querystring[queryindex];
							queryindex++;
							valueindex++;
						}

						if (MAXQUERYVALLEN <= valueindex)
						{
							IPSCAN_LOG( LOGPREFIX "ipscan: query parameter value string is too long : %s\n", querystring);
							valueindex = MAXQUERYVALLEN; // Truncate
						}
						valstring[valueindex]=0; // Add termination

						rc = sscanf(valstring,"%"SCNd64, &varval );
						if (1 == rc)
						{
							// Mark the entry as valid, increment the number of queries found
							query[numqueries].varval = varval;
							query[numqueries].valid = 1;
							#ifdef QUERYDEBUG
							IPSCAN_LOG( LOGPREFIX "ipscan: Added a new query name: %s with a value of : %"PRId64"\n", query[numqueries].varname, query[numqueries].varval);
							#endif
							numqueries++;
						}
						else
						{
							#ifdef QUERYDEBUG
							IPSCAN_LOG( LOGPREFIX "ipscan: Bad value assignment for %s, setting invalid.\n", query[numqueries].varname);
							#endif
							query[numqueries].varval = 0;
							query[numqueries].valid = 0;
							numqueries++;
						}
					}
					// Move past the '&' sign
					while ('&' == querystring[queryindex] && MAXQUERYSTRLEN > queryindex && 0 == finished)
					{
						queryindex++;
					}
					finished = (querystring[queryindex] < 32 || queryindex >= MAXQUERYSTRLEN) ? 1 : 0;
				}
				#ifdef QUERYDEBUG
				IPSCAN_LOG( LOGPREFIX "ipscan: Number of query pairs found is : %d\n", numqueries);
				#endif
			}
		}
		else if (0 == strncmp("HEAD", requestmethod, 4))
		{
			// Create the header
			HTML_HEADER();
			// Now finish the header
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("</html>\n");
			IPSCAN_LOG( LOGPREFIX "ipscan: HEAD request method, sending headers only\n");
			return(EXIT_SUCCESS);
		}
		else
		{
			IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: called with an unsupported request method: %s.\n", requestmethod);
			// Create the header
			HTML_HEADER();
			// Now finish the header
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>I was called with an unsupported request-method. That is very disappointing.</p>\n");
			// Finish the html
			create_html_body_end();
			return(EXIT_SUCCESS);
		}
	}

	// Determine the clients' address
	remoteaddrvar = getenv("REMOTE_ADDR");
	if(NULL == remoteaddrvar)
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: REMOTE_ADDR variable lookup returned NULL.\n");
	}
	else if (strnlen(remoteaddrvar,(INET6_ADDRSTRLEN+1)) > INET6_ADDRSTRLEN)
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: REMOTE_ADDR variable length exceeds allocated buffer size (%d > %d)\n", (int)strnlen(remoteaddrvar, (INET6_ADDRSTRLEN+1)), INET6_ADDRSTRLEN);
		// Create the header
		HTML_HEADER();
		// Now finish the header
		printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
		printf("</head>\n");
		printf("<body>\n");
		printf("<p>I was called with a REMOTE_ADDR variable that exceeds the supported size. That is very disappointing.</p>\n");
		// Finish the html
		create_html_body_end();
		return(EXIT_SUCCESS);
	}
	else if( sscanf(remoteaddrvar,"%"TO_STR(INET6_ADDRSTRLEN)"s",remoteaddrstring) != 1 )
	{
		IPSCAN_LOG( LOGPREFIX "ipscan: Invalid REMOTE_ADDR variable data.\n");
	}
	else
	{
		// Determine the remote host address
		rc = inet_pton(AF_INET6, remoteaddrstring, remotehost);
		if (rc <= 0)
		{
			IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: Unparseable IPv6 host address : %s\n", remoteaddrstring);
			// Create the header
			HTML_HEADER();
			// Now finish the header
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>I was called with an unparseable IPv6 host address. That is very disappointing.</p>\n");
			// Finish the html
			create_html_body_end();
			return(EXIT_SUCCESS);
		}
		else
		{
			remotehost_msb = 0ULL;
			remotehost_lsb = 0ULL;

			// Split address into two 64 bit values stored within database
			for (i=0 ; i<8 ; i++)
			{
				shift = 8 * (7-i);
				value = (remotehost[i]);
				while (shift > 0)
				{
					value = (value << 8);
					shift -= 8;
				}
				remotehost_msb |= value;
				shift = 8 * (7-i);
				value = (remotehost[8+i]);
				while (shift > 0)
				{
					value = (value << 8);
					shift -= 8;
				}
				remotehost_lsb |= value;
			}
		}
	}


	// If query string is empty then we generate the introductory html/form for the client

	if (0 == numqueries)
	{
		#ifdef CLIENTDEBUG
		#if (1 < IPSCAN_LOGVERBOSITY)
		IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: 0 queries\n",\
				(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
				(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
		#endif
		#endif

		// Create the HTML header
		HTML_HEADER();

		#ifdef IPSCAN_HTML5_ENABLED
		// Create the main HTML5 body
		create_html5_form(DEFNUMPORTS, NUMUDPPORTS, portlist, udpportlist);
		#else
		// Create the main html body
		create_html_form(DEFNUMPORTS, NUMUDPPORTS, portlist, udpportlist);
		#endif

		// Finish the html
		create_html_body_end();
	}

	// Following is a query, so determine the passed parameters and decide whether we
	// need to initiate a scan, return the current result set or a summary of scans

	else
	{
		#ifdef CLIENTDEBUG
		#if (1 < IPSCAN_LOGVERBOSITY)
		IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: %d queries\n",\
				(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
				(unsigned int)((remotehost_msb>>16) & 0xFFFF), numqueries );
		#endif
		#endif

		// includeexisting should only be passed the values -1 or 1, set to 0 if not present
		// or an unsuitable value is passed.
		i = 0;
		while (i < numqueries && 0 != strncmp("includeexisting",query[i].varname,15)) i++;
		if (i < numqueries && 1 == query[i].valid)
		{
			if ( 1 == abs((int)query[i].varval) )
			{
				includeexisting = (int)query[i].varval;
			}
			else
			{
				includeexisting = 0 ;
			}
		}
		else
		{
			includeexisting = 0;
		}

		// determine state of termsaccepted, if not present default to 0
		i = 0;
		while (i < numqueries && 0 != strncmp("termsaccepted",query[i].varname,13)) i++;
		if (i < numqueries && 1 == query[i].valid)
		{
			if ( 1 == abs((int)query[i].varval))
			{
				termsaccepted = 1;
			}
			else
			{
				termsaccepted = 0;
			}
		}
		else
		{
			termsaccepted = 0;
		}

		// Begin the reconstitution of the query string
		rc = snprintf(reconptr, reconquerysize, "includeexisting=%d", (int)includeexisting);
		if (16 < rc && 19 > rc)
		{
			reconptr += rc;
			reconquerysize -= (size_t)rc;
			if (0 >= reconquerysize)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: run out of room to reconstitute query, please increase MAXQUERYSTRLEN (%d) and recompile.\n", MAXQUERYSTRLEN);
				// Create the header
				HTML_HEADER();
				// Now finish the header
				printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
				printf("</head>\n");
				printf("<body>\n");
				printf("<p>I have run out of room to reconstitute the query. That is very disappointing.</p>\n");
				// Finish the html
				create_html_body_end();
				return(EXIT_SUCCESS);
			}
		}
		else
		{
			IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: attempt to reconstitute query returned an unexpected length (%d, expecting 17 or 18)\n", rc);
			// Create the header
			HTML_HEADER();
			// Now finish the header
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>I was called with an unexpected query length. That is very disappointing.</p>\n");
			// Finish the html
			create_html_body_end();
			return(EXIT_SUCCESS);
		}

		// Continue the reconstitution of the query string
		rc = snprintf(reconptr, reconquerysize, "&termsaccepted=%d", (int)termsaccepted);
		if (16 == rc)
		{
			reconptr += rc;
			reconquerysize -= (size_t)rc;
			if (reconquerysize <= 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: run out of room to continue reconstituting query, please increase MAXQUERYSTRLEN (%d) and recompile.\n", MAXQUERYSTRLEN);
				// Create the header
				HTML_HEADER();
				// Now finish the header
				printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
				printf("</head>\n");
				printf("<body>\n");
				printf("<p>I have run out of room to continue reconstituting the query. That is very disappointing.</p>\n");
				// Finish the html
				create_html_body_end();
				return(EXIT_SUCCESS);
			}
		}
		else
		{
			IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: attempt to reconstitute query returned an unexpected length (%d, expecting 16)\n", rc);
			// Create the header
			HTML_HEADER();
			// Now finish the header
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>I was called with an unexpected query length. That is very disappointing.</p>\n");
			// Finish the html
			create_html_body_end();
			return(EXIT_SUCCESS);
		}

		// Determine whether existing ports are to be included in the tested list or not:
		if (1 == includeexisting)
		{
			// custom ports will be appended to the default ports list
			numports = DEFNUMPORTS;
		}
		else
		{
			// default ports will be overwritten by any custom ports
			numports = 0;
		}

		#ifdef QUERYDEBUG
		IPSCAN_LOG( LOGPREFIX "ipscan: numports is initially found to be %d\n", numports);
		#endif

		//
		// Add in the custom ports if they're valid and NOT already present in the portlist ...
		//

		int customport = 0;
		char cpnum[17];
		size_t cplen;

		// Counter holding the number of received customportN statements
		unsigned int numcustomports = 0;

		while (NUMUSERDEFPORTS > customport)
		{
			cplen = (size_t)snprintf(cpnum, 16, "customport%d", customport);
			i = 0;
			while (i < numqueries && 0 != strncmp(cpnum,query[i].varname,cplen)) i++;

			// If customportN parameter exists then increment the counter, irrespective of whether
			// the parameter was valid or not
			if (i < numqueries) numcustomports++;

			// If the parameter is valid then perform further checks
			if (i < numqueries && 1 == query[i].valid)
			{
				// Check the port number is in the valid range
				if (query[i].varval >=MINVALIDPORT && query[i].varval <= MAXVALIDPORT)
				{
					j = 0;
					while (j < numports && portlist[j].port_num != query[i].varval) j++;
					// if this customport is not one of the ports already destined for checking then
					// add it to the port list
					if (j == numports)
					{
						portlist[numports].port_num = (uint16_t)(query[i].varval & VALIDPORTMASK);
						portlist[numports].special = 0;
						rc = snprintf(&portlist[numports].port_desc[0], PORTDESCSIZE, "User-specified: %d",(int)query[i].varval);
						if (rc < 0 || rc >= PORTDESCSIZE)
						{
							IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: failed to write user-specified port description, does PORTDESCSIZE (%d) need increasing?\n", PORTDESCSIZE);
						}
						numports ++;
						rc = snprintf(reconptr, reconquerysize, "&customport%d=%d", customport, (int)query[i].varval);
						// &customport (11); cpnum (1-5) ; = (1) ; portnum (1-5)
						if (rc >= 14 && rc <= 22)
						{
							reconptr += rc;
							reconquerysize -= (size_t)rc;
							if (reconquerysize <= 0)
							{
								IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: run out of room to reconstitute query, please increase MAXQUERYSTRLEN (%d) and recompile.\n", MAXQUERYSTRLEN);
								// Create the header
								HTML_HEADER();
								// Now finish the header
								printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
								printf("</head>\n");
								printf("<body>\n");
								printf("<p>I have run out of room to reconstitute the query. That is very disappointing.</p>\n");
								// Finish the html
								create_html_body_end();
								return(EXIT_SUCCESS);
							}
						}
						else
						{
							IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: customport%d reconstitution failed, due to unexpected size.\n", customport);
							// Create the header
							HTML_HEADER();
							// Now finish the header
							printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
							printf("</head>\n");
							printf("<body>\n");
							printf("<p>I have run out of room to reconstitute the query. That is very disappointing.</p>\n");
							// Finish the html
							create_html_body_end();
							return(EXIT_SUCCESS);
						}
					}
				}
			}
			customport++;
		}

		// Look for the starttime query string, set it to -1 if not present or invalid
		i = 0;
		querystarttime = -1;
		while (i < numqueries && strncmp("starttime",query[i].varname,9)!= 0) i++;
		if (i < numqueries && query[i].valid == 1)
		{
			if (query[i].varval >= 0)
			{
				querystarttime = query[i].varval;
			}
		}

		// Look for the session query string, set it to -1 if not present or invalid
		i = 0;
		querysession = -1;
		while (i < numqueries && strncmp("session",query[i].varname,7)!= 0) i++;
		if (i < numqueries && query[i].valid == 1)
		{
			if (query[i].varval >= 0)
			{
				querysession = query[i].varval;
			}
		}

		// Look for the beginscan query string, return 0 if not present or incorrect value
		i = 0;
		beginscan = 0;
		while (i < numqueries && strncmp("beginscan",query[i].varname,9)!= 0) i++;
		if (i < numqueries && query[i].valid == 1)
		{
			beginscan = (query[i].varval == MAGICBEGIN ) ? 1 : 0;
		}

		// Look for the fetch query string
		i = 0;
		fetch = 0;
		while (i < numqueries && strncmp("fetch",query[i].varname,5)!= 0) i++;
		if (i < numqueries && query[i].valid == 1)
		{
			fetch = (query[i].varval >0) ? 1 : 0;
			#if (TEXTMODE != 1)
			if (1 == fetch && (int)(query[i].varval < 4096)) fetchnum = (int)query[i].varval;
			#endif
		}

		// Dump the variables resulting from the query-string parsing
		#ifdef QUERYDEBUG
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: numqueries = %d\n", numqueries);
		#if (TEXTMODE != 1)
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: includeexisting = %d beginscan = %d fetch = %d fetchnum = %d\n", includeexisting, beginscan, fetch, fetchnum);
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: querysession = %"PRId64" querystarttime = %"PRId64"\n", querysession, querystarttime );
		#else
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: includeexisting = %d beginscan = %d fetch = %d\n", includeexisting, beginscan, fetch);
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: session = %"PRIu64" starttime = %"PRIu64" and numports = %d\n", \
				session, (uint64_t)starttime, numports);
		#endif
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: numcustomports = %d NUMUSERDEFPORTS = %d\n", numcustomports, NUMUSERDEFPORTS );
		IPSCAN_LOG( LOGPREFIX "ipscan: DEBUG info: reconstituted query string = %s\n", reconquery );
		#endif

		//
		//
		//
		// NOW DETERMINE WHAT TO DO ......
		//
		//
		//

		#if (TEXTMODE == 1)

		char stimeresult[32]; // function calls for at least 26 characters
		char * stptr = NULL;

		// ----------------------------------------------------------------------
		//
		// Start of text-mode only cases
		//
		// ----------------------------------------------------------------------

		// *IF* we have everything we need to initiate the scan/results page then we
		// should have been passed (2+NUMUSERDEFPORTS) queries
		// i.e. includeexisting (either +1 or -1), termsaccepted and customports 0 thru n params

		if ( numqueries >= (NUMUSERDEFPORTS + 2) && (numcustomports == NUMUSERDEFPORTS) && 0 != includeexisting && 1 == termsaccepted )
		{
			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: text-mode, initiate scan\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			#if (1 <= IPSCAN_LOGVERBOSITY)
			time_t scanstart = starttime;
			#endif

			// Create the header
			HTML_HEADER();
			// Create main output
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<h3 style=\"color:red\">IPv6 Port Scan Results for host %s</h3>\n", remoteaddrstring);
			stptr = ctime_r(&starttime,stimeresult);
			if (NULL == stptr)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR - text-mode ctime_r() failed\n");
			}
			else
			{
				printf("<p>Scan beginning at: %s, expected to take up to %d seconds ...</p>\n", \
						stimeresult, (int)ESTIMATEDTIMETORUN );
			}

			// Log termsaccepted
			IPSCAN_LOG( LOGPREFIX "ipscan: Client: %x:%x:%x:: beginning with termsaccepted = %d\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF), termsaccepted );
			IPSCAN_LOG( LOGPREFIX "ipscan: at time %"PRIu64", session %"PRIu64"\n", (uint64_t)starttime, (uint64_t)session);

			// Only included if ping is compiled in ...
			#if (IPSCAN_INCLUDE_PING == 1)
			// Ping the remote host and store the result ...
			pingresult = check_icmpv6_echoresponse(remoteaddrstring, (uint64_t)starttime, (uint64_t)session, indirecthost);
			result = (pingresult >= IPSCAN_INDIRECT_RESPONSE) ? (pingresult - IPSCAN_INDIRECT_RESPONSE) : pingresult ;

			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: ICMPv6 ping of client %s returned %d (%s), from host %s\n",remoteaddrstring, pingresult, resultsstruct[result].label, indirecthost);
			#else
			IPSCAN_LOG( LOGPREFIX "ipscan: ICMPv6 ping of client: %x:%x:%x::\n",\
				(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
				(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			portsstats[result]++ ;

			rc = write_db(remotehost_msb, remotehost_lsb, (uint64_t)starttime, (uint64_t)session, (0 + (IPSCAN_PROTO_ICMPV6 << IPSCAN_PROTO_SHIFT)), pingresult, indirecthost);
			if (rc != 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR : write_db for ping result returned : %d\n", rc);
			}

			printf("<p>ICMPv6 ECHO-Request:</p>\n");
			printf("<table border=\"1\">\n");
			printf("<tr style=\"text-align:left\">\n");
			if (pingresult >= IPSCAN_INDIRECT_RESPONSE)
			{
				printf("<td title=\"IPv6 ping\">ICMPv6 ECHO REQUEST returned : </td><td style=\"background-color:%s\">INDIRECT-%s (from %s)</td>\n",resultsstruct[result].colour,resultsstruct[result].label, indirecthost);
			}
			else
			{
				printf("<td title=\"IPv6 ping\">ICMPv6 ECHO REQUEST returned : </td><td style=\"background-color:%s\">%s</td>\n",resultsstruct[result].colour,resultsstruct[result].label);
			}
			printf("</tr>\n");
			printf("</table>\n");
			#endif

			#if (1 == IPSCAN_INCLUDE_UDP)
			// Log UDP start of scan
			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of %d UDP ports on client : %s\n", numudpports, remoteaddrstring);
			#else
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of UDP ports on client  : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			// Scan the UDP ports in parallel
			remaining = numudpports;
			porti = 0;
			numchildren = 0;
			rc = 0;
			while (remaining > 0 || numchildren > 0)
			{
				while (remaining > 0)
				{
					if (numchildren < MAXUDPCHILDREN && remaining > 0)
					{
						unsigned int todo = (remaining > MAXUDPPORTSPERCHILD) ? MAXUDPPORTSPERCHILD : (unsigned int)remaining;
						#ifdef UDPPARLLDEBUG
						IPSCAN_LOG( LOGPREFIX "ipscan: check_udp_ports_parll(%s,%d,%d,host_msb,host_lsb,starttime,session,portlist)\n",remoteaddrstring,porti,todo);
						#endif
						rc |= check_udp_ports_parll(remoteaddrstring, porti, todo, remotehost_msb, remotehost_lsb, (uint64_t)starttime, session, &udpportlist[0]);
						porti += todo;
						numchildren ++;
						remaining = (int)(numudpports - porti);
					}
					if (numchildren == MAXUDPCHILDREN && remaining > 0)
					{
						int pid = wait(&childstatus);
						numchildren--;
						if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: UDP ongoing phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
					}
				}
				while (numchildren > 0)
				{
					int pid = wait(&childstatus);
					numchildren--;
					if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: UDP shutdown phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
				}
			}

			if (rc != 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: check_udp_ports_parll() exited with ORed value of %d\n",rc);
			}

			printf("<p>Individual UDP port scan results:</p>\n");
			// Start of UDP port scan results table
			printf("<table border=\"1\">\n");
			for (portindex= 0; portindex < NUMUDPPORTS ; portindex++)
			{
				port = udpportlist[portindex].port_num;
				special = udpportlist[portindex].special;
				last = (portindex == (NUMUDPPORTS-1)) ? 1 : 0 ;
				result = read_db_result(remotehost_msb, remotehost_lsb, (uint64_t)starttime, (uint64_t)session, (uint32_t)(port + ((special & IPSCAN_SPECIAL_MASK) << IPSCAN_SPECIAL_SHIFT) + (IPSCAN_PROTO_UDP << IPSCAN_PROTO_SHIFT) ));
				if ( PORTUNKNOWN == result )
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: read_db_result() returned UNKNOWN: UDP port scan results table\n" );
					IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
							(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
							(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
					IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n",\
							querystarttime, querysession);
				}

				#ifdef UDPDEBUG
				if (0 != special)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: UDP port %d:%d returned %d(%s)\n", port, special, result, resultsstruct[result].label);
				}
				else
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: UDP port %d returned %d(%s)\n", port, result, resultsstruct[result].label);
				}
				#endif

				// Start of a new row, so insert the appropriate tag if required
				if (position ==0) printf("<tr>");

				// Find a matching returnval, or else flag it as unknown
				i = 0 ;
				while (i < NUMRESULTTYPES && resultsstruct[i].returnval != result) i++;
				if (result == resultsstruct[i].returnval)
				{
					portsstats[result]++ ;
					if (0 != special)
					{
						printf("<td title=\"%s\" style=\"background-color:%s\">Port %d[%d] = %s</td>", udpportlist[portindex].port_desc, resultsstruct[i].colour, port, special, resultsstruct[i].label);
					}
					else
					{
						printf("<td title=\"%s\" style=\"background-color:%s\">Port %d = %s</td>", udpportlist[portindex].port_desc, resultsstruct[i].colour, port, resultsstruct[i].label);
					}
				}
				else
				{
					if (0 != special)
					{
						printf("<td title=\"%s\" style=\"background-color:white\">Port %d[%d] = BAD</td>", udpportlist[portindex].port_desc, port, special);
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: Unknown result for UDP port %d:%d is %d\n", port, special, result);
					}
					else
					{
						printf("<td title=\"%s\" style=\"background-color:white\">Port %d = BAD</td>", udpportlist[portindex].port_desc, port);
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: Unknown result for UDP port %d is %d\n", port, result);
					}
					portsstats[ PORTUNKNOWN ]++ ;
				}

				// Get ready for the next cell, add the end of row tag if required
				position++;
				if (position >= TXTMAXCOLS || last == 1) { printf("</tr>\n"); position=0; };

			}
			printf("</table>\n");
			#endif

			//
			// TCP scan is always included
			//
			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of %d TCP ports on client : %s\n", numports, remoteaddrstring);
			#else
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of TCP ports on client  : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif
			printf("<p>Individual TCP port scan results:</p>\n");

			// Scan the TCP ports in parallel
			remaining = (int)numports;
			porti = 0;
			numchildren = 0;
			rc = 0;
			while (remaining > 0 || numchildren > 0)
			{
				while (remaining > 0)
				{
					if (numchildren < MAXCHILDREN && remaining > 0)
					{
						unsigned int todo = (remaining > MAXPORTSPERCHILD) ? MAXPORTSPERCHILD : (unsigned int)remaining;
						#ifdef PARLLDEBUG
						IPSCAN_LOG( LOGPREFIX "ipscan: check_tcp_ports_parll(%s,%d,%d,host_msb,host_lsb,starttime,session,portlist)\n",remoteaddrstring,porti,todo);
						#endif
						rc |= check_tcp_ports_parll(remoteaddrstring, porti, todo, remotehost_msb, remotehost_lsb, (uint64_t)starttime, (uint64_t)session, &portlist[0]);
						porti += todo;
						numchildren ++;
						remaining = (int)(numports - porti);
					}
					if (numchildren == MAXCHILDREN && remaining > 0)
					{
						int pid = wait(&childstatus);
						numchildren--;
						if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: ongoing phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
					}
				}
				while (numchildren > 0)
				{
					int pid = wait(&childstatus);
					numchildren--;
					if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: shutdown phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
				}
			}

			if (rc != 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: check_tcp_ports_parll() exited with ORed value of %d\n",rc);
			}

			// Start of TCP port scan results table
			printf("<table border=\"1\">\n");
			for (portindex= 0; portindex < numports ; portindex++)
			{
				port = portlist[portindex].port_num;
				special = portlist[portindex].special;
				last = (portindex == (numports-1)) ? 1 : 0 ;
				result = read_db_result(remotehost_msb, remotehost_lsb, (uint64_t)starttime, (uint64_t)session, (uint32_t)(port + ((special & IPSCAN_SPECIAL_MASK) << IPSCAN_SPECIAL_SHIFT)+ (IPSCAN_PROTO_TCP << IPSCAN_PROTO_SHIFT)) );
				if ( PORTUNKNOWN == result )
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: read_db_result() returned UNKNOWN: TCP port scan results table\n" );
					IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
							(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
							(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
					IPSCAN_LOG( LOGPREFIX "ipscan: at starttime %"PRIu64", session %"PRIu64"\n",\
							(uint64_t)starttime, (uint64_t)session);
				}

				#ifdef RESULTSDEBUG
				if (0 != special)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: TCP port %d:%d returned %d(%s)\n", port, special, result, resultsstruct[result].label);
				}
				else
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: TCP port %d returned %d(%s)\n", port, result, resultsstruct[result].label);
				}
				#endif

				// Start of a new row, so insert the appropriate tag if required
				if (position ==0) printf("<tr>");

				// Find a matching returnval, or else flag it as unknown
				i = 0 ;
				while (i < NUMRESULTTYPES && resultsstruct[i].returnval != result) i++;
				if (result == resultsstruct[i].returnval)
				{
					portsstats[result]++ ;
					if (0 != special)
					{
						printf("<td title=\"%s\" style=\"background-color:%s\">Port %d[%d] = %s</td>", portlist[portindex].port_desc, resultsstruct[i].colour, port, special, resultsstruct[i].label);
					}
					else
					{
						printf("<td title=\"%s\" style=\"background-color:%s\">Port %d = %s</td>", portlist[portindex].port_desc, resultsstruct[i].colour, port, resultsstruct[i].label);
					}

				}
				else
				{
					if (0 != special)
					{
						printf("<td title=\"%s\" style=\"background-color:white\">Port %d[%d] = BAD</td>", portlist[portindex].port_desc, port, special);
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: Unknown result for TCP port %d:%d is %d\n", port, special, result);
					}
					else
					{
						printf("<td title=\"%s\" style=\"background-color:white\">Port %d = BAD</td>", portlist[portindex].port_desc, port);
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: Unknown result for TCP port %d is %d\n",port,result);
					}
					portsstats[ PORTUNKNOWN ]++ ;
				}

				// Get ready for the next cell, add the end of row tag if required
				position++;
				if (position >= TXTMAXCOLS || last == 1) { printf("</tr>\n"); position=0; };

			}
			printf("</table>\n");

			char fintimeresult[32]; // ctime requires 26 bytes
			char * ftptr = NULL;
			time_t nowtime = time(0);
			ftptr = ctime_r(&nowtime, fintimeresult);

			if (nowtime < 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for nowtime %d (%s)\n", errno, strerror(errno));
			}
			else
			{
				if (NULL == ftptr)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: finish time ctime_r() returned NULL\n");
				}
				else
				{
					printf("<p>Scan of %d ports complete at: %s.</p>\n", numports, fintimeresult);
				}
			}

			// Create results key table
			create_results_key_table(remoteaddrstring, starttime);
			// Finish the output
			create_html_body_end();

			#if (1 <= IPSCAN_LOGVERBOSITY)
			time_t scancomplete = time(0);
			if (scancomplete < 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for scancomplete %d (%s)\n", errno, strerror(errno));
			}
			IPSCAN_LOG( LOGPREFIX "ipscan: port scan and html document generation took %d seconds\n", (int)(scancomplete - scanstart));
			#endif

			// Log the summary of results internally
			i = 0;
			position = 0;
			while (i < NUMRESULTTYPES)
			{
				if (position == 0)
				{
					rc = snprintf(logbufferptr, logbuffersize, "Found %d %s",portsstats[i], resultsstruct[i].label );
				}
				else
				{
					rc = snprintf(logbufferptr, logbuffersize, ", %d %s", portsstats[i], resultsstruct[i].label);
				}

				if (rc < 0 || rc >= (int)logbuffersize)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: logbuffer write truncated, increase LOGENTRYLEN (currently %d) and recompile.\n", LOGENTRYLEN);
					break;
				}

				logbufferptr += rc ;
				logbuffersize -= (size_t)rc;
				position ++ ;
				if ( position >= LOGMAXCOLS || i == (NUMRESULTTYPES -1) )
				{
					#if (1 <= IPSCAN_LOGVERBOSITY)
					IPSCAN_LOG( LOGPREFIX "ipscan: %s\n", logbuffer);
					#endif
					logbufferptr = &logbuffer[0];
					logbuffersize = LOGENTRYLEN;
					position = 0;
				}
				i++ ;
			}

			// Delete our results now that we're done
			rc = delete_from_db(remotehost_msb, remotehost_lsb, (uint64_t)starttime, (uint64_t)session);
			if (0 != rc)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: delete_from_db return code was %d (expected 0)\n", rc);
				return(EXIT_SUCCESS);
			}
		}

		// ----------------------------------------------------------------------
		//
		// End of text-mode only cases
		//
		// ----------------------------------------------------------------------

		#else

		// ----------------------------------------------------------------------
		//
		// Start of javascript-mode only cases
		//
		// ----------------------------------------------------------------------

		// *IF* we have everything we need to query the database ...
		// (1)querysession, (2)querystarttime, (3)fetch, (4)includeexisting and (5)termsaccepted. 
		// Could also have one or more customports. 
		// This statement handles cases where fetch indicates completion/failure.

		if ( numqueries >= 5 && querysession >= 0 && querystarttime >= 0 && beginscan == 0 && fetch == 1 \
				&& termsaccepted == 1 && includeexisting != 0 && IPSCAN_SUCCESSFUL_COMPLETION <= fetchnum)
		{
			#ifdef CLIENTDEBUG
			char fetchstring[IPSCAN_FETCHNUM_STRING_MAX+1];
			fetch_to_string(fetchnum, &fetchstring[0]);
			IPSCAN_LOG( LOGPREFIX "ipscan: Fetch indicated %s completion for client : %x:%x:%x::\n", fetchstring,\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
			#endif

			// Put out a dummy page to keep the webserver happy
			HTML_HEADER();
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>End of test - dummy response.</p>\n");
			// Finish the output
			create_html_body_end();
			// Fetch running state result from database so it can be updated
			result = read_db_result(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession, (0 + (IPSCAN_PROTO_TESTSTATE << IPSCAN_PROTO_SHIFT) ) );
			if ( PORTUNKNOWN == result )
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: read_db_result() returned UNKNOWN: fetching running state\n" );
				IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
				IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n",\
					querystarttime, querysession);
				// Set state to running but flag that database returned something unexpected
				result = ( IPSCAN_TESTSTATE_RUNNING_BIT | IPSCAN_TESTSTATE_DATABASE_ERROR_BIT );
				IPSCAN_LOG( LOGPREFIX "ipscan: running state changed to indicate DATABASE error\n" );
				rc = write_db(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession,\
					 (0 + (IPSCAN_PROTO_TESTSTATE << IPSCAN_PROTO_SHIFT)), result, unusedfield);
				if (rc != 0)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: write_db for IPSCAN_PROTO_TESTSTATE rewrite returned non-zero: %d\n", rc);
				}
			}

			if (IPSCAN_SUCCESSFUL_COMPLETION == fetchnum)
			{
				// Overwrite any other bits in this ONE case
				result = IPSCAN_TESTSTATE_COMPLETE_BIT;
			}
			else if (IPSCAN_HTTPTIMEOUT_COMPLETION == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_HTTPTIMEOUT_BIT; 
			}
			else if (IPSCAN_EVAL_ERROR == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_EVALERROR_BIT;
			}
			else if (IPSCAN_OTHER_ERROR == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_OTHERERROR_BIT; 
			}
			else if (IPSCAN_UNSUCCESSFUL_COMPLETION == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_BADCOMPLETE_BIT;
			}
			else if (IPSCAN_NAVIGATE_AWAY == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_NAVAWAY_BIT; 
			}
			else if (IPSCAN_BAD_JSON_ERROR == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_EVALERROR_BIT;
			}
			else if (IPSCAN_UNEXPECTED_CHANGE == fetchnum)
			{
				result |= IPSCAN_TESTSTATE_UNEXPCHANGE_BIT; 
			}
			else
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: fetch included unexpected value %d for client : %x:%x:%x::\n",\
						fetchnum, (unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
						(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
				IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
				result |= IPSCAN_TESTSTATE_OTHERERROR_BIT; 
				IPSCAN_LOG( LOGPREFIX "ipscan: running state changed to indicate OTHER error\n" );
			}
			// Write the new value back to the database
			rc = update_db(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession, (0 + (IPSCAN_PROTO_TESTSTATE << IPSCAN_PROTO_SHIFT)), result, unusedfield);
			if (0 != rc)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: update_db for IPSCAN_TESTSTATE UPDATE returned non-zero: %d\n", rc);
			}
		}

		// *IF* we have everything we need to query the database ...
		// (1)querysession, (2)querystarttime, (3)fetch, (4)includeexisting and (5)termsaccepted. 
		// Could also have one or more customports.
		// Check that fetch number is less than a value which indicates completion/failure

		else if ( numqueries >= 5 && querysession >= 0 && querystarttime >= 0 && beginscan == 0 && fetch == 1 \
				&& termsaccepted == 1 && includeexisting != 0  && IPSCAN_SUCCESSFUL_COMPLETION > fetchnum)
		{
			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: javascript-mode, query database fetch\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
			#endif

			// Simplified header in which to wrap array of results
			create_json_header();
			// Dump the current port results for this client, querystarttime and querysession
			rc = dump_db(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession);
			if (rc != 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: dump_db return code was %d (expected 0)\n", rc);
				return(EXIT_SUCCESS);
			}
		}

		// *IF* we have everything we need to initiate the scan
		// (1)querysession, (2)querystarttime, (3)beginscan, (4)termsaccepted, (5)includeexisting
		// Could also have one or more customports.
		// Check that there is no fetch query.

		else if ( numqueries >= 5 && querysession >= 0 && querystarttime >= 0 && beginscan == 1 \
				&& termsaccepted == 1 && includeexisting != 0 && fetch == 0)
		{
			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: javascript-mode, initiate scan\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
			#endif

			// Generate database entry for test state - indicate test running
			rc = write_db(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession,\
					 (0 + (IPSCAN_PROTO_TESTSTATE << IPSCAN_PROTO_SHIFT)), IPSCAN_TESTSTATE_RUNNING_BIT, unusedfield);
			if (rc != 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: write_db for IPSCAN_PROTO_TESTSTATE RUNNING returned non-zero: %d\n", rc);
			}

			time_t scanstart = time(0);
			if (scanstart < 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for scanstart %d (%s)\n", errno, strerror(errno));
			}

			// Put out a dummy page to keep the webserver happy
			// Creating this page will take the entire duration of the scan ...
			HTML_HEADER();
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>Initiate scan.</p>\n");
			// Finish the output
			create_html_body_end();

			#ifdef CLIENTDEBUG
			#if (1 <= IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: write_db to set IPSCAN_PROTO_TESTSTATE RUNNING for client : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
			#endif
			#endif

			// Log terms accepted
			IPSCAN_LOG( LOGPREFIX "ipscan: Client: %x:%x:%x:: beginning with termsaccepted = %d\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF), termsaccepted );
			IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);

			// Only include this section if ping is compiled in ...
			#if (IPSCAN_INCLUDE_PING == 1)
			pingresult = check_icmpv6_echoresponse(remoteaddrstring, (uint64_t)querystarttime, (uint64_t)querysession, indirecthost);
			result = (pingresult >= IPSCAN_INDIRECT_RESPONSE) ? (pingresult - IPSCAN_INDIRECT_RESPONSE) : pingresult ;
			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: ICMPv6 ping of client %s returned %d (%s), from host %s\n",remoteaddrstring,\
					 pingresult, resultsstruct[result].label, indirecthost);
			#else
			IPSCAN_LOG( LOGPREFIX "ipscan: ICMPv6 ping of client: %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif
			portsstats[result]++ ;
			rc = write_db(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession,\
					(0 + (IPSCAN_PROTO_ICMPV6 << IPSCAN_PROTO_SHIFT)), pingresult, indirecthost);
			if (rc != 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: write_db for ping result returned non-zero: %d\n", rc);
				create_html_body_end();
				return(EXIT_SUCCESS);
			}
			#endif

			// Only included if UDP is compiled in ...
			#if (IPSCAN_INCLUDE_UDP == 1)

			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of %d UDP ports on client : %s\n", numudpports, remoteaddrstring);
			#else
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of UDP ports on client  : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			// Scan the UDP ports in parallel
			remaining = (int)numudpports;
			porti = 0;
			numchildren = 0;
			while (remaining > 0 || numchildren > 0)
			{
				while (remaining > 0)
				{
					if (numchildren < MAXUDPCHILDREN && remaining > 0)
					{
						unsigned int todo = (remaining > MAXUDPPORTSPERCHILD) ? MAXUDPPORTSPERCHILD : (unsigned int)remaining;
						#ifdef UDPPARLLDEBUG
						IPSCAN_LOG( LOGPREFIX "ipscan: check_udp_ports_parll(%s,%d,%d,host_msb,host_lsb,querystarttime,querysession,portlist)\n",\
							remoteaddrstring,porti,todo);
						#endif
						rc = check_udp_ports_parll(remoteaddrstring, porti, todo, remotehost_msb, remotehost_lsb, (uint64_t)querystarttime,\
							(uint64_t)querysession, &udpportlist[0]);
						porti += todo;
						numchildren ++;
						remaining = (int)(numudpports - porti);
					}
					if (numchildren == MAXUDPCHILDREN && remaining > 0)
					{
						int pid = wait(&childstatus);
						numchildren--;
						if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: UDP ongoing phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
					}
				}
				while (numchildren > 0)
				{
					int pid = wait(&childstatus);
					numchildren--;
					if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: UDP shutdown phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
				}
			}
			#endif

			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of %d TCP ports on client : %s\n", numports, remoteaddrstring);
			#else
			IPSCAN_LOG( LOGPREFIX "ipscan: Beginning scan of TCP ports on client  : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			// Scan the TCP ports in parallel
			remaining = (int)numports;
			porti = 0;
			numchildren = 0;
			while (remaining > 0 || numchildren > 0)
			{
				while (remaining > 0)
				{
					if (numchildren < MAXCHILDREN && remaining > 0)
					{
						unsigned int todo = (remaining > MAXPORTSPERCHILD) ? MAXPORTSPERCHILD : (unsigned int)remaining;
						#ifdef PARLLDEBUG
						IPSCAN_LOG( LOGPREFIX "ipscan: check_tcp_ports_parll(%s,%d,%d,host_msb,host_lsb,querystarttime,querysession,portlist)\n",remoteaddrstring,porti,todo);
						#endif
						rc = check_tcp_ports_parll(remoteaddrstring, porti, todo, remotehost_msb, remotehost_lsb,\
								 (uint64_t)querystarttime, (uint64_t)querysession, &portlist[0]);
						porti += todo;
						numchildren ++;
						remaining = (int)(numports - porti);
					}
					if (numchildren == MAXCHILDREN && remaining > 0)
					{
						int pid = wait(&childstatus);
						numchildren--;
						if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: ongoing phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
					}
				}
				while (numchildren > 0)
				{
					int pid = wait(&childstatus);
					numchildren--;
					if (childstatus != 0) IPSCAN_LOG( LOGPREFIX "ipscan: WARNING: shutdown phase : PID=%d retired with status=%d, numchildren is now %d\n", pid, childstatus, numchildren );
				}
			}

			// Only included if UDP is compiled in ...
			#if (IPSCAN_INCLUDE_UDP == 1)
			// Generate the stats
			for (portindex= 0; portindex < NUMUDPPORTS ; portindex++)
			{
				port = udpportlist[portindex].port_num;
				special = udpportlist[portindex].special;
				result = read_db_result(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession,\
					(uint32_t)(port + ((special & IPSCAN_SPECIAL_MASK) << IPSCAN_SPECIAL_SHIFT) + (IPSCAN_PROTO_UDP << IPSCAN_PROTO_SHIFT) ) );
				if ( PORTUNKNOWN == result )
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: read_db_result() returned UNKNOWN: UDP creating stats\n" );
					IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
						(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
						(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
					IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
				}

				// Find a matching returnval, or else flag it as unknown
				i = 0 ;
				while (i < NUMRESULTTYPES && resultsstruct[i].returnval != result) i++;
				if (result == resultsstruct[i].returnval)
				{
					portsstats[result]++ ;
				}
				else
				{
					if (0 != special)
					{
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING scan of UDP port %d:%d returned : %d\n", port, special, result);
					}
					else
					{
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING scan of UDP port %d returned : %d\n", port, result);
					}
					portsstats[PORTUNKNOWN]++;
				}
			}
			#endif

			for (portindex= 0; portindex < numports ; portindex++)
			{
				port = portlist[portindex].port_num;
				special = portlist[portindex].special;
				result = read_db_result(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession, (uint32_t)(port + ((special & IPSCAN_SPECIAL_MASK) << IPSCAN_SPECIAL_SHIFT) + (IPSCAN_PROTO_TCP << IPSCAN_PROTO_SHIFT) ));
				if ( PORTUNKNOWN == result )
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: read_db_result() returned UNKNOWN: TCP creating stats\n" );
					IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
							(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
							(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
					IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
				}

				// Find a matching returnval, or else flag it as unknown
				i = 0 ;
				while (i < NUMRESULTTYPES && resultsstruct[i].returnval != result) i++;
				if (result == resultsstruct[i].returnval)
				{
					portsstats[result]++ ;
				}
				else
				{
					if (0 != special)
					{
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING scan of TCP port %d:%d returned : %d\n", port, special, result);
					}
					else
					{
						IPSCAN_LOG( LOGPREFIX "ipscan: WARNING scan of TCP port %d returned : %d\n", port, result);
					}
					portsstats[PORTUNKNOWN]++;
				}
			}

			#if (1 < IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: rmthost        was : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			IPSCAN_LOG( LOGPREFIX "ipscan: querystarttime was : %"PRId64"\n", querystarttime);
			IPSCAN_LOG( LOGPREFIX "ipscan: querysession   was : %"PRId64"\n", querysession);
			IPSCAN_LOG( LOGPREFIX "ipscan: numcustomports was : %d\n", numcustomports);
			#endif


			#if (1 <= IPSCAN_LOGVERBOSITY)
			time_t scancomplete = time(0);
			if (scancomplete < 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for scancomplete %d (%s)\n", errno, strerror(errno));
			}
			IPSCAN_LOG( LOGPREFIX "ipscan: port scan and html document generation took %d seconds\n", (int)(scancomplete - scanstart));
			#endif

			// Log the summary of results internally
			i = 0;
			position = 0;
			while (i < NUMRESULTTYPES)
			{
				if (position == 0)
				{
					rc = snprintf(logbufferptr, logbuffersize, "Found %d %s",portsstats[i], resultsstruct[i].label );
				}
				else
				{
					rc = snprintf(logbufferptr, logbuffersize, ", %d %s", portsstats[i], resultsstruct[i].label);
				}

				if (rc < 0 || rc >= (int)logbuffersize)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: logbuffer write truncated, increase LOGENTRYLEN (currently %d) and recompile.\n", LOGENTRYLEN);
					break;
				}

				logbufferptr += rc ;
				logbuffersize -= (size_t)rc;
				position ++ ;
				if ( position >= LOGMAXCOLS || i == (NUMRESULTTYPES -1) )
				{
					#if (1 <= IPSCAN_LOGVERBOSITY)
					IPSCAN_LOG( LOGPREFIX "ipscan: %s\n", logbuffer);
					#endif
					logbufferptr = &logbuffer[0];
					logbuffersize = LOGENTRYLEN;
					position = 0;
				}
				i++ ;
			}

			// Wait until the javascript client has flagged the test as complete or we've run out of time ...
			#ifdef CLIENTDEBUG
			char flags[IPSCAN_FLAGSBUFFER_SIZE+1];
			char * flagsrc = NULL;
			memset(flags, 0, sizeof(flags));
			#endif

			unsigned int client_finished = 0;
			time_t timeouttime = (scanstart + IPSCAN_DELETE_TIMEOUT);
			time_t deletenowtime = time(0);

			if (deletenowtime < 0)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for first deletenowtime %d (%s)\n", errno, strerror(errno));
				deletenowtime = timeouttime;
			}

			//
			// wait for client to signal test complete or timeout
			//
			while (deletenowtime < timeouttime && client_finished == 0)
			{
				result = read_db_result(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession,\
						 (0 + (IPSCAN_PROTO_TESTSTATE << IPSCAN_PROTO_SHIFT) ) );
				if ( PORTUNKNOWN == result )
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: read_db_result() returned UNKNOWN: waiting for test end\n" );
					IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
							(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
							(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
					IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
					result = IPSCAN_TESTSTATE_DATABASE_ERROR_BIT;
				}

				#ifdef CLIENTDEBUG
				flagsrc = state_to_string(result, &flags[0], (int)IPSCAN_FLAGSBUFFER_SIZE);

				#if (1 <= IPSCAN_LOGVERBOSITY)
				IPSCAN_LOG( LOGPREFIX "ipscan: waiting for IPSCAN_TESTSTATE_COMPLETE, IPSCAN_TESTSTATE value is currently: %d\n", result);
				#endif

				if (NULL != flagsrc)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: IPSCAN_TESTSTATE for client : %x:%x:%x:: %s\n",\
							(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
							(unsigned int)((remotehost_msb>>16) & 0xFFFF), flagsrc );
					IPSCAN_LOG( LOGPREFIX "ipscan: at querytime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
				}
				#endif

				// Check whether the client has signalled the test is complete - various reasons
				if (IPSCAN_TESTSTATE_COMPLETE_BIT == (result & IPSCAN_TESTSTATE_COMPLETE_BIT) \
						|| (IPSCAN_TESTSTATE_BADCOMPLETE_BIT == (result & IPSCAN_TESTSTATE_BADCOMPLETE_BIT)))
				{
					client_finished = 1;
				}
				else
				{
					// Otherwise sleep before checking again
					sleep(IPSCAN_TESTSTATE_COMPLETE_SLEEP);
				}

				deletenowtime = time(0);
				if (deletenowtime < 0)
				{
					IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: time() returned bad value for deletenowtime %d (%s)\n", errno, strerror(errno));
					deletenowtime = timeouttime;
				}
			} // end of wait for client to signal test complete or timeout

			#ifdef CLIENTDEBUG
			char cdstartres[32]; // ctime_r requires 26 chars
			char cdtimeoutres[32]; // ctime_r requires 26 chars
			char * cds_ptr = NULL;
			char * cdt_ptr = NULL;
			cds_ptr = ctime_r(&scanstart, cdstartres);
			cdt_ptr = ctime_r(&timeouttime, cdtimeoutres);

			if (1 == client_finished)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: Exited test-complete loop because client signalled.\n");
			}
			else
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: Exited test-complete loop with no client response.\n");
				if (NULL != cds_ptr) IPSCAN_LOG( LOGPREFIX "ipscan: starttime   was : %d (%s)\n", (int)scanstart, cdstartres );
				if (NULL != cdt_ptr) IPSCAN_LOG( LOGPREFIX "ipscan: timeouttime was : %d (%s)\n", (int)timeouttime, cdtimeoutres);
			}
			#endif

			// Wait so that errant/delayed JSON fetches are likely to subside
			sleep( IPSCAN_DELETE_WAIT_PERIOD );

			// Delete the results
			//
			rc = delete_from_db(remotehost_msb, remotehost_lsb, (uint64_t)querystarttime, (uint64_t)querysession);
			if (0 != rc)
			{
				IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: delete_from_db return code was %d (expected 0)\n", rc);
			}
		}

		// *IF* we have everything we need to create the standard HTML page
		// we should have been passed (2+NUMUSERDEFPORTS) queries
		// i.e. (+1)includeexisting (either +1 or -1) and (+2)termsaccepted and NUMUSERDEFPORTS

		else if (numqueries >= (NUMUSERDEFPORTS + 2) && numcustomports == NUMUSERDEFPORTS && includeexisting != 0 && beginscan == 0 \
				&& termsaccepted == 1 && fetch == 0)
		{
			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: javascript-mode, create start page\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			#if (1 <= IPSCAN_LOGVERBOSITY)
			IPSCAN_LOG( LOGPREFIX "ipscan: Creating the standard web results page start point\n");

			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: for client : %x:%x:%x::\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			IPSCAN_LOG( LOGPREFIX "ipscan: at querystarttime %"PRId64", querysession %"PRId64"\n", querystarttime, querysession);
			#endif
			#endif

			// Create the header and body
			#if (IPSCAN_INCLUDE_UDP == 1)
			create_html_header(numports, numudpports, reconquery);
			create_html_body(remoteaddrstring, starttime, numports, numudpports, portlist, udpportlist);
			#else
			create_html_header(numports, 0, reconquery);
			create_html_body(remoteaddrstring, starttime, numports, 0, portlist, udpportlist);
			#endif
			// Create the main html body
			create_html_body_end();
		}

		// ----------------------------------------------------------------------
		//
		// End of java-script only cases
		//
		// ----------------------------------------------------------------------

		#endif

		// ----------------------------------------------------------------------
		//
		// Cases common to both modes of operation
		//
		// ----------------------------------------------------------------------

		else if (termsaccepted == 0)
		{
			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: common-mode, terms not accepted\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			// Tell the user that they haven't accepted the terms and conditions
			HTML_HEADER();

			printf("<title>IPv6 Port Scanner - Terms and Conditions MUST be accepted BEFORE use</title>\n");
			printf("</head>\n");

			printf("<body>\n");
			printf("<h3 style=\"color:red\">IPv6 Port Scanner Terms and Conditions MUST be accepted BEFORE use</h3>\n");
			printf("<p>IPscan testing cannot continue until the terms and conditions of use have been accepted. ");
			printf("You seem to have presented an incomplete or unexpected query string to IPscan.</p>\n");
			#if (IPSCAN_BAD_URL_HELP != 0)
			printf("<p>If you are trying to automate IPscan operation then please see the following ");
			printf("<a href=\"%s\">Scan Automation link</a> for commonly used examples. ", IPSCAN_BAD_URL_LINK);
			printf("Assuming that you accept the terms and conditions of use, then you might just be missing an \
			 \"&amp;termsaccepted=1\" term from the provided query-string.</p>\n");
			#endif
			#if (IPSCAN_TC_MISSING_LINK != 0)
			printf("<p style=\"font-weight:bold\">Please <a href=\"%s\">click here</a> to start again.</p>\n", IPSCAN_TC_MISSING_LINK_URL);
			#endif
			// Finish the output
			create_html_body_end();
			IPSCAN_LOG( LOGPREFIX "ipscan: Something untoward happened, numqueries = %d\n", numqueries);
			IPSCAN_LOG( LOGPREFIX "ipscan: includeexisting = %d, beginscan = %d, fetch = %d,\n", includeexisting, beginscan, fetch);
			IPSCAN_LOG( LOGPREFIX "ipscan: querysession = %"PRId64" querystarttime = %"PRId64" numports = %d and numcustomports = %d.\n", \
					querysession, querystarttime, numports, numcustomports);
			IPSCAN_LOG( LOGPREFIX "ipscan: Client: %x:%x:%x:: beginning with termsaccepted = %d\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF), termsaccepted );
		}

		else
		{
			#ifdef CLIENTDEBUG
			IPSCAN_LOG( LOGPREFIX "ipscan: Remote host address %x:%x:%x:: common-mode, final else - hack?\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF) );
			#endif

			// Dummy report - most likely to be triggered via a hackers attempt to pass unusual query parameters
			HTML_HEADER();
			printf("<title>IPv6 Port Scanner Version %s</title>\n", IPSCAN_VER);
			printf("</head>\n");
			printf("<body>\n");
			printf("<p>Nothing useful to report.</p>\n");
			#if (IPSCAN_BAD_URL_HELP != 0)
			printf("<p>You seem to have presented an incomplete or unexpected query string to IPscan. ");
			printf("If you are trying to automate IPscan operation then please see the following ");
			printf("<a href=\"%s\">Scan Automation link.</a></p>\n", IPSCAN_BAD_URL_LINK);
			#endif
			// Finish the output
			create_html_body_end();
			// Log information relevant to the event
			IPSCAN_LOG( LOGPREFIX "ipscan: Something untoward happened, numqueries = %d\n", numqueries);
			IPSCAN_LOG( LOGPREFIX "ipscan: includeexisting = %d, beginscan = %d, fetch = %d,\n", includeexisting, beginscan, fetch);
			IPSCAN_LOG( LOGPREFIX "ipscan: querysession = %"PRId64" querystarttime = %"PRId64" numports = %d and numcustomports = %d.\n", \
					querysession, querystarttime, numports, numcustomports);
			IPSCAN_LOG( LOGPREFIX "ipscan: Client: %x:%x:%x:: beginning with termsaccepted = %d\n",\
					(unsigned int)((remotehost_msb>>48) & 0xFFFF), (unsigned int)((remotehost_msb>>32) & 0xFFFF),\
					(unsigned int)((remotehost_msb>>16) & 0xFFFF), termsaccepted );
		}
	}
	// Call tidy_up_db() to purge any expired results 
	if (starttime > 0)
	{
		rc = tidy_up_db( (uint64_t)starttime );
		if (0 != rc) IPSCAN_LOG( LOGPREFIX "ipscan: ERROR: tidy_up_db() returned %d\n", rc);
	}
	return(EXIT_SUCCESS);
}
