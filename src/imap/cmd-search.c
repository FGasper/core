/* Copyright (C) 2002 Timo Sirainen */

#include "common.h"
#include "ostream.h"
#include "str.h"
#include "commands.h"
#include "imap-search.h"

#define STRBUF_SIZE 1024

static int imap_search(struct client *client, const char *charset,
		       struct mail_search_arg *sargs)
{
        struct mail_search_context *ctx;
        struct mailbox_transaction_context *trans;
	const struct mail *mail;
	string_t *str;
	int ret, uid, first = TRUE;

	str = t_str_new(STRBUF_SIZE);
	uid = client->cmd_uid;

	trans = mailbox_transaction_begin(client->mailbox, FALSE);
	ctx = mailbox_search_init(trans, charset, sargs,
				  NULL, 0, NULL);
	if (ctx == NULL) {
		mailbox_transaction_rollback(trans);
		return FALSE;
	}

	str_append(str, "* SEARCH");
	while ((mail = mailbox_search_next(ctx)) != NULL) {
		if (str_len(str) >= STRBUF_SIZE-MAX_INT_STRLEN) {
			/* flush */
			o_stream_send(client->output,
				      str_data(str), str_len(str));
			str_truncate(str, 0);
			first = FALSE;
		}

		str_printfa(str, " %u", uid ? mail->uid : mail->seq);
	}

	ret = mailbox_search_deinit(ctx);

	if (mailbox_transaction_commit(trans, 0) < 0)
		ret = -1;

	if (!first || ret == 0) {
		str_append(str, "\r\n");
		o_stream_send(client->output, str_data(str), str_len(str));
	}
	return ret == 0;
}

int cmd_search(struct client *client)
{
	struct mail_search_arg *sargs;
	struct imap_arg *args;
	int args_count;
	const char *error, *charset;

	args_count = imap_parser_read_args(client->parser, 0, 0, &args);
	if (args_count < 1) {
		if (args_count == -2)
			return FALSE;

		client_send_command_error(client, args_count < 0 ? NULL :
					  "Missing SEARCH arguments.");
		return TRUE;
	}

	if (!client_verify_open_mailbox(client))
		return TRUE;

	if (args->type == IMAP_ARG_ATOM &&
	    strcasecmp(IMAP_ARG_STR(args), "CHARSET") == 0) {
		/* CHARSET specified */
		args++;
		if (args->type != IMAP_ARG_ATOM &&
		    args->type != IMAP_ARG_STRING) {
			client_send_command_error(client,
						  "Invalid charset argument.");
			return TRUE;
		}

		charset = IMAP_ARG_STR(args);
		args++;
	} else {
		charset = NULL;
	}

	sargs = imap_search_args_build(client->cmd_pool, client->mailbox, args, &error);
	if (sargs == NULL) {
		/* error in search arguments */
		client_send_tagline(client, t_strconcat("NO ", error, NULL));
	} else if (imap_search(client, charset, sargs)) {
		return cmd_sync(client, MAILBOX_SYNC_FLAG_FAST |
				(client->cmd_uid ?
				 0 : MAILBOX_SYNC_FLAG_NO_EXPUNGES),
				"OK Search completed.");
	} else {
		client_send_storage_error(client,
					  mailbox_get_storage(client->mailbox));
	}

	return TRUE;
}
