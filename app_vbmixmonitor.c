/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief MixMonitor() - Record a call and mix the audio during the recording
 * \ingroup applications
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * \note Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 375484 $")

#include "asterisk/paths.h"	/* use ast_config_AST_MONITOR_DIR */
#include "asterisk/file.h"
#include "asterisk/audiohook.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/autochan.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include <ifaddrs.h>
//#include "asterisk/config_options.h"
#include <curl/curl.h>
#include "cJSON.h"
#include "voicebase.h"

#define ast_alloca(size) __builtin_alloca(size)

/*** DOCUMENTATION
	<application name="VBMixMonitor" language="en_US">
		<synopsis>
			Record a call and mix the audio during the recording.  Use of StopMixMonitor is required
			to guarantee the audio file is available for processing during dialplan execution.
		</synopsis>
		<syntax>
			<parameter name="file" required="true" argsep=".">
				<argument name="filename" required="true">
					<para>If <replaceable>filename</replaceable> is an absolute path, uses that path, otherwise
					creates the file in the configured monitoring directory from <filename>asterisk.conf.</filename></para>
				</argument>
				<argument name="extension" required="true" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Append to the file instead of overwriting it.</para>
					</option>
					<option name="b">
						<para>Only save audio to the file while the channel is bridged.</para>
						<note><para>Does not include conferences or sounds played to each bridged party</para></note>
						<note><para>If you utilize this option inside a Local channel, you must make sure the Local
						channel is not optimized away. To do this, be sure to call your Local channel with the
						<literal>/n</literal> option. For example: Dial(Local/start@mycontext/n)</para></note>
					</option>
					<option name="v">
						<para>Adjust the <emphasis>heard</emphasis> volume by a factor of <replaceable>x</replaceable>
						(range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="V">
						<para>Adjust the <emphasis>spoken</emphasis> volume by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="W">
						<para>Adjust both, <emphasis>heard and spoken</emphasis> volumes by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
				</optionlist>
			</parameter>
			<parameter name="command">
				<para>Will be executed when the recording is over.</para>
				<para>Any strings matching <literal>^{X}</literal> will be unescaped to <variable>X</variable>.</para>
				<para>All variables will be evaluated at the time MixMonitor is called.</para>
			</parameter>
		</syntax>
		<description>
			<para>Records the audio on the current channel to the specified file.</para>
			<para>This application does not automatically answer and should be preceeded by
			an application such as Answer or Progress().</para>
			<note><para>MixMonitor runs as an audiohook. In order to keep it running through
			a transfer, AUDIOHOOK_INHERIT must be set for the channel which ran mixmonitor.
			For more information, including dialplan configuration set for using
			AUDIOHOOK_INHERIT with MixMonitor, see the function documentation for
			AUDIOHOOK_INHERIT.</para></note>
			<variablelist>
				<variable name="MIXMONITOR_FILENAME">
					<para>Will contain the filename used to record.</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Monitor</ref>
			<ref type="application">StopMixMonitor</ref>
			<ref type="application">PauseMonitor</ref>
			<ref type="application">UnpauseMonitor</ref>
			<ref type="function">AUDIOHOOK_INHERIT</ref>
		</see-also>
	</application>
	<application name="StopVBMixMonitor" language="en_US">
		<synopsis>
			Stop recording a call through MixMonitor, and free the recording's file handle.
		</synopsis>
		<syntax />
		<description>
			<para>Stops the audio recording that was started with a call to <literal>MixMonitor()</literal>
			on the current channel.</para>
		</description>
		<see-also>
			<ref type="application">MixMonitor</ref>
		</see-also>
	</application>
	<manager name="VBMixMonitorMute" language="en_US">
		<synopsis>
			Mute / unMute a Mixmonitor recording.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to mute.</para>
			</parameter>
			<parameter name="Direction">
				<para>Which part of the recording to mute:  read, write or both (from channel, to channel or both channels).</para>
			</parameter>
			<parameter name="State">
				<para>Turn mute on or off : 1 to turn on, 0 to turn off.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to mute a MixMonitor recording.</para>
		</description>
	</manager>

 ***/

#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

static const char * const app = "VBMixMonitor";

static const char * const stop_app = "StopVBMixMonitor";

static const char * const mixmonitor_spy_type = "VBMixMonitor";

struct mixmonitor {
	struct ast_audiohook audiohook;
	char*	name;

	struct cJSON *params;

	struct ast_autochan *autochan;
	struct mixmonitor_ds *mixmonitor_ds;
};


struct mixmonitor_ds {
	unsigned int destruction_ok;
	ast_cond_t destruction_condition;
	ast_mutex_t lock;

	/* The filestream is held in the datastore so it can be stopped
	 * immediately during stop_mixmonitor or channel destruction. */
	int fs_quit;
	struct ast_filestream *fs;
	struct ast_audiohook *audiohook;
};

/*!
 * \internal
 * \pre mixmonitor_ds must be locked before calling this function
 */
static void mixmonitor_ds_close_fs(struct mixmonitor_ds *mixmonitor_ds)
{
	if (mixmonitor_ds->fs) {
		ast_closestream(mixmonitor_ds->fs);
		mixmonitor_ds->fs = NULL;
		mixmonitor_ds->fs_quit = 1;
		ast_verb(2, "VBMixMonitor close filestream\n");
	}
}

static void mixmonitor_ds_destroy(void *data)
{
	struct mixmonitor_ds *mixmonitor_ds = data;

	ast_mutex_lock(&mixmonitor_ds->lock);
	mixmonitor_ds->audiohook = NULL;
	mixmonitor_ds->destruction_ok = 1;
	ast_cond_signal(&mixmonitor_ds->destruction_condition);
	ast_mutex_unlock(&mixmonitor_ds->lock);
}

static const struct ast_datastore_info mixmonitor_ds_info = {
	.type = "vbmixmonitor",
	.destroy = mixmonitor_ds_destroy,
};

static void destroy_monitor_audiohook(struct mixmonitor *mixmonitor)
{
	if (mixmonitor->mixmonitor_ds) {
		ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
		mixmonitor->mixmonitor_ds->audiohook = NULL;
		ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
	}
	/* kill the audiohook.*/
	ast_audiohook_lock(&mixmonitor->audiohook);
	ast_audiohook_detach(&mixmonitor->audiohook);
	ast_audiohook_unlock(&mixmonitor->audiohook);
	ast_audiohook_destroy(&mixmonitor->audiohook);
}

static int startmon(struct ast_channel *chan, struct ast_audiohook *audiohook)
{
	struct ast_channel *peer = NULL;
	int res = 0;

	if (!chan)
		return -1;

	ast_audiohook_attach(chan, audiohook);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan)))
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);

	return res;
}

#define SAMPLES_PER_FRAME 160

static void mixmonitor_free(struct mixmonitor *mixmonitor)
{
	if (mixmonitor) {
		if (mixmonitor->mixmonitor_ds) {
			ast_mutex_destroy(&mixmonitor->mixmonitor_ds->lock);
			ast_cond_destroy(&mixmonitor->mixmonitor_ds->destruction_condition);
			ast_free(mixmonitor->mixmonitor_ds);
		}

		mixmonitor->params = NULL;

		ast_free(mixmonitor);
	}
}
static void *mixmonitor_thread(void *obj)
{
	struct mixmonitor *mixmonitor = obj;
	long int prev_ts = 0;
	int count = 0;
	long int cts = 0;
	struct mem_storage_t mem_storage;

	ast_verb(2, "Begin VBMixMonitor Recording %s\n", mixmonitor->name);

	if (!create_mem_storage(&mem_storage, mixmonitor->params)){
		ast_log(LOG_ERROR, "Can't allocate memory for segment data storage\n");
	}

	/* The audiohook must enter and exit the loop locked */
	ast_audiohook_lock(&mixmonitor->audiohook);
	while (mixmonitor->audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING && !mixmonitor->mixmonitor_ds->fs_quit) {
		struct ast_frame *fr = NULL;

		if (!(fr = ast_audiohook_read_frame(&mixmonitor->audiohook, SAMPLES_PER_FRAME, AST_AUDIOHOOK_DIRECTION_BOTH, AST_FORMAT_SLINEAR))) {
			ast_audiohook_trigger_wait(&mixmonitor->audiohook);

			if (mixmonitor->audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
				break;
			}
			continue;
		}

		/* audiohook lock is not required for the next block.
		 * Unlock it, but remember to lock it before looping or exiting */
		ast_audiohook_unlock(&mixmonitor->audiohook);

		if ( 1 ) {
			struct ast_frame *cur;

			ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);

			for (cur = fr; cur && !mixmonitor->mixmonitor_ds->fs_quit; cur = AST_LIST_NEXT(cur, frame_list)) {

				if (cts - prev_ts > get_vb_segment_duration() * 1000 && (is_opened(&mem_storage))){
					close_mem_storage(&mem_storage, 0);
					ast_log(LOG_WARNING, "Closed file storage\n");
					prev_ts = cts;
					++count;
				}

				/* Initialize the file if not already done so */
				if (!is_opened(&mem_storage)) {
					open_mem_storage(&mem_storage, mixmonitor->autochan->chan->name, count, cts);
				}

				put_data(&mem_storage, cur);

				cts += ast_codec_get_samples(cur) * 1000 / 8000; //we assume here that simple wav format always has 8kHz sample rate
																	//and this is true while we ask AST_FORMAT_SLINEAR from hook
			}

			ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
		}
		/* All done! free it. */
		ast_frame_free(fr, 0);

		ast_audiohook_lock(&mixmonitor->audiohook);
	}

	if (!is_opened(&mem_storage)) {
		open_mem_storage(&mem_storage, mixmonitor->autochan->chan->name, count,cts);
		put_silence(&mem_storage, 8000);
	}

	if (is_opened(&mem_storage)){
		close_mem_storage(&mem_storage, 1);
	}

	destroy_mem_storage(&mem_storage);

	/* Test Event */
	ast_test_suite_event_notify("VBMIXMONITOR_END", "Channel: %s\r\n",
									mixmonitor->autochan->chan->name);

	ast_audiohook_unlock(&mixmonitor->audiohook);

	ast_autochan_destroy(mixmonitor->autochan);

	/* Datastore cleanup.  close the filestream and wait for ds destruction */
	ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
	mixmonitor_ds_close_fs(mixmonitor->mixmonitor_ds);
	if (!mixmonitor->mixmonitor_ds->destruction_ok) {
		ast_cond_wait(&mixmonitor->mixmonitor_ds->destruction_condition, &mixmonitor->mixmonitor_ds->lock);
	}
	ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);

	/* kill the audiohook */
	destroy_monitor_audiohook(mixmonitor);

	ast_verb(2, "End VBMixMonitor Recording %s\n", mixmonitor->name);
	mixmonitor_free(mixmonitor);
	return NULL;
}

static int setup_mixmonitor_ds(struct mixmonitor *mixmonitor, struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct mixmonitor_ds *mixmonitor_ds;

	if (!(mixmonitor_ds = ast_calloc(1, sizeof(*mixmonitor_ds)))) {
		return -1;
	}

	ast_mutex_init(&mixmonitor_ds->lock);
	ast_cond_init(&mixmonitor_ds->destruction_condition, NULL);

	if (!(datastore = ast_datastore_alloc(&mixmonitor_ds_info, NULL))) {
		ast_mutex_destroy(&mixmonitor_ds->lock);
		ast_cond_destroy(&mixmonitor_ds->destruction_condition);
		ast_free(mixmonitor_ds);
		return -1;
	}

	mixmonitor_ds->audiohook = &mixmonitor->audiohook;
	datastore->data = mixmonitor_ds;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	mixmonitor->mixmonitor_ds = mixmonitor_ds;
	return 0;
}

static void launch_monitor_thread(struct ast_channel *chan, char* command_line)
{
	pthread_t thread;
	struct mixmonitor *mixmonitor;
	size_t len;

	len = sizeof(*mixmonitor) + strlen(chan->name) + strlen(command_line) + 2;


	/* Pre-allocate mixmonitor structure and spy */
	if (!(mixmonitor = ast_calloc(1, len))) {
		return;
	}

	/* Setup the actual spy before creating our thread */
	if (ast_audiohook_init(&mixmonitor->audiohook, AST_AUDIOHOOK_TYPE_SPY, mixmonitor_spy_type)) {
		mixmonitor_free(mixmonitor);
		return;
	}

	/* Copy over flags and channel name */
	if (!(mixmonitor->autochan = ast_autochan_setup(chan))) {
		mixmonitor_free(mixmonitor);
		return;
	}

	if (setup_mixmonitor_ds(mixmonitor, chan)) {
		ast_autochan_destroy(mixmonitor->autochan);
		mixmonitor_free(mixmonitor);
		return;
	}
	mixmonitor->name = (char *) mixmonitor + sizeof(*mixmonitor);
	strcpy(mixmonitor->name, chan->name);
	mixmonitor->params = (char *) mixmonitor + sizeof(*mixmonitor) + strlen(mixmonitor->name) + 1;
	strcpy(mixmonitor->params, command_line);

	ast_set_flag(&mixmonitor->audiohook, AST_AUDIOHOOK_TRIGGER_SYNC);

	if (startmon(chan, &mixmonitor->audiohook)) {
		ast_log(LOG_WARNING, "Unable to add '%s' spy to channel '%s'\n",
			mixmonitor_spy_type, chan->name);
		ast_audiohook_destroy(&mixmonitor->audiohook);
		mixmonitor_free(mixmonitor);
		return;
	}

	ast_pthread_create_detached_background(&thread, NULL, mixmonitor_thread, mixmonitor);
}

static int mixmonitor_exec(struct ast_channel *chan, const char *data)
{

	launch_monitor_thread(chan, data);

	return 0;
}

static int stop_mixmonitor_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *datastore = NULL;

	ast_channel_lock(chan);
	ast_audiohook_detach_source(chan, mixmonitor_spy_type);
	if ((datastore = ast_channel_datastore_find(chan, &mixmonitor_ds_info, NULL))) {
		struct mixmonitor_ds *mixmonitor_ds = datastore->data;

		ast_mutex_lock(&mixmonitor_ds->lock);

		/* closing the filestream here guarantees the file is avaliable to the dialplan
	 	 * after calling StopMixMonitor */
		mixmonitor_ds_close_fs(mixmonitor_ds);

		/* The mixmonitor thread may be waiting on the audiohook trigger.
		 * In order to exit from the mixmonitor loop before waiting on channel
		 * destruction, poke the audiohook trigger. */
		if (mixmonitor_ds->audiohook) {
			ast_audiohook_lock(mixmonitor_ds->audiohook);
			ast_cond_signal(&mixmonitor_ds->audiohook->trigger);
			ast_audiohook_unlock(mixmonitor_ds->audiohook);
			mixmonitor_ds->audiohook = NULL;
		}

		ast_mutex_unlock(&mixmonitor_ds->lock);

		/* Remove the datastore so the monitor thread can exit */
		if (!ast_channel_datastore_remove(chan, datastore)) {
			ast_datastore_free(datastore);
		}
	}
	ast_channel_unlock(chan);

	return 0;
}

static char *handle_cli_mixmonitor(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "vbmixmonitor {start|stop}";
		e->usage =
			"Usage: vbmixmonitor <start|stop> <chan_name> [args]\n"
			"       The optional arguments are passed to the VBMixMonitor\n"
			"       application when the 'start' command is used.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	if (!(chan = ast_channel_get_by_name_prefix(a->argv[2], strlen(a->argv[2])))) {
		ast_cli(a->fd, "No channel matching '%s' found.\n", a->argv[2]);
		/* Technically this is a failure, but we don't want 2 errors printing out */
		return CLI_SUCCESS;
	}

	ast_channel_lock(chan);

	if (!strcasecmp(a->argv[1], "start")) {
		mixmonitor_exec(chan, a->argv[3]);
		ast_channel_unlock(chan);
	} else {
		ast_channel_unlock(chan);
		ast_audiohook_detach_source(chan, mixmonitor_spy_type);
	}

	chan = ast_channel_unref(chan);

	return CLI_SUCCESS;
}

/*! \brief  Mute / unmute  a MixMonitor channel */
static int manager_mute_mixmonitor(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;

	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *state = astman_get_header(m, "State");
	const char *direction = astman_get_header(m,"Direction");

	int clearmute = 1;

	enum ast_audiohook_flags flag;

	if (ast_strlen_zero(direction)) {
		astman_send_error(s, m, "No direction specified. Must be read, write or both");
		return AMI_SUCCESS;
	}

	if (!strcasecmp(direction, "read")) {
		flag = AST_AUDIOHOOK_MUTE_READ;
	} else  if (!strcasecmp(direction, "write")) {
		flag = AST_AUDIOHOOK_MUTE_WRITE;
	} else  if (!strcasecmp(direction, "both")) {
		flag = AST_AUDIOHOOK_MUTE_READ | AST_AUDIOHOOK_MUTE_WRITE;
	} else {
		astman_send_error(s, m, "Invalid direction specified. Must be read, write or both");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(state)) {
		astman_send_error(s, m, "No state specified");
		return AMI_SUCCESS;
	}

	clearmute = ast_false(state);
	c = ast_channel_get_by_name(name);

	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_audiohook_set_mute(c, mixmonitor_spy_type, flag, clearmute)) {
		c = ast_channel_unref(c);
		astman_send_error(s, m, "Cannot set mute flag");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	astman_append(s, "\r\n");

	c = ast_channel_unref(c);

	return AMI_SUCCESS;
}

static struct ast_cli_entry cli_mixmonitor[] = {
	AST_CLI_DEFINE(handle_cli_mixmonitor, "Execute a VBMixMonitor command")
};


/*!
 * \internal \brief Load the configuration information
 * \param reload If non-zero, this is a reload operation; otherwise, it is an initial module load
 * \retval 0 on success
 * \retval non-zero on error
 */
static int load_configuration(int reload)
{
    struct ast_config *cfg;
    char *cat = NULL;
    struct ast_variable *var;
    struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
    int res = 0;

    cfg = ast_config_load("vbmixmonitor.conf", config_flags);
    if (!cfg) {
        ast_log(LOG_ERROR, "Config file vbmixmonitor.conf failed to load\n");
        return 1;
    } else if (cfg == CONFIG_STATUS_FILEINVALID) {
        ast_log(LOG_ERROR, "Config file vbmixmonitor.conf is invalid\n");
        return 1;
    } else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
        return 0;
    }

    set_defaults();

    /* We could theoretically use ast_variable_retrieve, but since
       that can traverse all of the variables in a category on each call,
       its often better to just traverse the variables in a context
       in a single pass. */
    while ((cat = ast_category_browse(cfg, cat))) {

        /* Our config file only has a general section for now */
        if (strcasecmp(cat, "general")) {
            continue;
        }

        var = ast_variable_browse(cfg, cat);
        while (var) {
            if (!strcasecmp(var->name, "segment_length")) {
                int sl_temp;
                if (sscanf(var->value, "%30d", &sl_temp) != 1) {
                    ast_log(AST_LOG_WARNING, "Failed to parse segment_length value [%s]\n", var->value);
                    res = 1;
                    goto cleanup;
                }
                if (sl_temp <= 1 || sl_temp > 600) {
                    ast_log(AST_LOG_WARNING, "Invalid value %d for segment_length: must be between %d and %d\n",
                    		sl_temp, 1, 600);
                    res = 1;
                    goto cleanup;
                }
                set_vb_segment_duration(sl_temp);
            } else if (!strcasecmp(var->name, "api_key")) {
                set_vb_api_key(var->value);
            } else if (!strcasecmp(var->name, "password")) {
            	set_vb_password(var->value);
            } else if (!strcasecmp(var->name, "public")) {
				set_vb_public(var->value);
            } else if (!strcasecmp(var->name, "callback_url")) {
				set_vb_callback_url(var->value);
            } else if (!strcasecmp(var->name, "api_url")) {
            	set_vb_api_url(var->value);
            } else if (!strcasecmp(var->name, "title")) {
            	set_vb_title(var->value);
            } else {
                ast_log(AST_LOG_WARNING, "Unknown configuration key %s\n", var->name);
            }

            var = var->next;
        }
    }

cleanup:
    ast_config_destroy(cfg);
    return res;
}

static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_mixmonitor, ARRAY_LEN(cli_mixmonitor));
	res = ast_unregister_application(stop_app);
	res |= ast_unregister_application(app);
	res |= ast_manager_unregister("VBMixMonitorMute");

	return res;
}

static int load_module(void)
{
	int res;
	
	ast_log(LOG_NOTICE,"VBMixMonitor loaded\n");

	ast_cli_register_multiple(cli_mixmonitor, ARRAY_LEN(cli_mixmonitor));
	res = ast_register_application_xml(app, mixmonitor_exec);
	res |= ast_register_application_xml(stop_app, stop_mixmonitor_exec);
	res |= ast_manager_register_xml("VBMixMonitorMute", 0, manager_mute_mixmonitor);

	if (load_configuration(0)) {
		res |= AST_MODULE_LOAD_DECLINE;
	}else
		res |= AST_MODULE_LOAD_SUCCESS;
		
	curl_global_init(CURL_GLOBAL_ALL);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Voicebase Mixed Audio Monitoring Application");
