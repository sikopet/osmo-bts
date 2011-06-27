/* Main program for Sysmocom BTS */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sys/signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/application.h>

#include <osmo-bts/gsm_data.h>
#include <osmo-bts/logging.h>
#include <osmo-bts/abis.h>
#include <osmo-bts/bts.h>
#include <osmo-bts/vty.h>

#include "l1_if.h"

/* FIXME: read from real hardware */
const uint8_t abis_mac[6] = { 0,1,2,3,4,5 };
/* FIXME: generate from git */
const char *software_version = "0815";

static const char *bsc_host = NULL;
static const char *config_file = "osmo-bts.cfg";
extern const char *osmobts_copyright;
static int daemonize = 0;

int bts_model_init(struct gsm_bts *bts)
{
	struct femtol1_hdl *fl1h;

	fl1h = l1if_open(bts->c0);
	if (!fl1h) {
		LOGP(DL1C, LOGL_FATAL, "Cannot open L1 Interface\n");
		return -EIO;
	}

	bts->c0->role_bts.l1h = fl1h;

	l1if_reset(fl1h);

	return 0;
}

struct ipabis_link *link_init(struct gsm_bts *bts, const char *ip)
{
	struct ipabis_link *link = talloc_zero(bts, struct ipabis_link);
	struct in_addr ia;
	int rc;

	inet_aton(ip, &ia);

	link->bts = bts;
	bts->oml_link = link;

	rc = abis_open(link, ntohl(ia.s_addr));
	if (rc < 0)
		return NULL;

	return link;
}

static void print_help()
{
	printf( "Some useful options:\n"
		"  -h	--help		this text\n"
		"  -d	--debug MASK	Enable debugging (e.g. -d DRSL:DOML:DLAPDM)\n"
		"  -D	--daemonize	For the process into a background daemon\n"
		"  -c	--config-file 	Specify the filename of the config file\n"
		"  -s	--disable-color	Don't use colors in stderr log output\n"
		"  -T	--timestamp	Prefix every log line with a timestamp\n"
		"  -V	--version	Print version information and exit\n"
		"  -e 	--log-level	Set a global log-level\n"
		"  -B	--bsc-host	Specify host-name of the BSC\n"
		);
}

/* FIXME: finally get some option parsing code into libosmocore */
static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_idx = 0, c;
		static const struct option long_options[] = {
			/* FIXME: all those are generic Osmocom app options */
			{ "help", 0, 0, 'h' },
			{ "debug", 1, 0, 'd' },
			{ "daemonize", 0, 0, 'D' },
			{ "config-file", 1, 0, 'c' },
			{ "disable-color", 0, 0, 's' },
			{ "timestamp", 0, 0, 'T' },
			{ "version", 0, 0, 'V' },
			{ "log-level", 1, 0, 'e' },
			{ "bsc-host", 1, 0, 'B' },
			{ 0, 0, 0, 0 }
		};

		c = getopt_long(argc, argv, "hd:Dc:sTVe:B:",
				long_options, &option_idx);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_help();
			exit(0);
			break;
		case 's':
			log_set_use_color(osmo_stderr_target, 0);
			break;
		case 'd':
			log_parse_category_mask(osmo_stderr_target, optarg);
			break;
		case 'D':
			daemonize = 1;
			break;
		case 'c':
			config_file = strdup(optarg);
			break;
		case 'T':
			log_set_print_timestamp(osmo_stderr_target, 1);
			break;
		case 'V':
			print_version(1);
			exit(0);
			break;
		case 'e':
			log_set_log_level(osmo_stderr_target, atoi(optarg));
			break;
		case 'B':
			bsc_host = strdup(optarg);
			break;
		default:
			break;
		}
	}
}

static void signal_handler(int signal)
{
	fprintf(stderr, "signal %u received\n", signal);

	switch (signal) {
	case SIGINT:
		//osmo_signal_dispatch(SS_GLOBAL, S_GLOBAL_SHUTDOWN, NULL);
		sleep(3);
		exit(0);
		break;
	case SIGABRT:
	case SIGUSR1:
	case SIGUSR2:
		talloc_report_full(tall_bts_ctx, stderr);
		break;
	default:
		break;
	}
}

int main(int argc, char **argv)
{
	struct gsm_bts *bts;
	struct ipabis_link *link;
	int rc;

	tall_bts_ctx = talloc_named_const(NULL, 1, "OsmoBTS context");

	bts_log_init(NULL);

	bts = gsm_bts_alloc(tall_bts_ctx);

	vty_init(&bts_vty_info);
	//bts_vty_init(&

	/* FIXME: parse config file */

	rc = telnet_init(tall_bts_ctx, NULL, 4241);
	if (rc < 0) {
		fprintf(stderr, "Error initializing telnet\n");
		exit(1);
	}

	handle_options(argc, argv);

	if (!bsc_host) {
		fprintf(stderr, "You need to specify the BSC hostname\n");
		exit(2);
	}

	signal(SIGINT, &signal_handler);
	signal(SIGABRT, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	osmo_init_ignore_signals();

	if (bts_init(bts) < 0) {
		fprintf(stderr, "unable to to open bts\n");
		exit(1);
	}

	link = link_init(bts, bsc_host);
	if (!link) {
		fprintf(stderr, "unable to connect to BSC\n");
		exit(1);
	}
	bts->oml_link = link;

	if (daemonize) {
		rc = osmo_daemonize();
		if (rc < 0) {
			perror("Error during daemonize");
			exit(1);
		}
	}

	while (1) {
		log_reset_context();
		osmo_select_main(0);
	}
}