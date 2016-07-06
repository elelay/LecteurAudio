# -*- coding: utf-8 -*-
# Extension script to synchronize episodes with LecteurAudio
# Requirements: gPodder 3.x
# (c) 2016 Eric Le Lay <contact@elelay.fr>
# Released under the same license terms as gPodder itself.

import contextlib
import inspect
import os.path
import subprocess
from subprocess import CalledProcessError
import sys
import time

import gtk

import gpodder
from gpodder import util

import traceback

import logging
logger = logging.getLogger(__name__)

_ = gpodder.gettext

__title__ = _('Sync to LecteurAudio')
__description__ = _('synchronize episodes with LecteurAudio')
__author__ = 'Eric Le Lay <contact@elelay.fr>'
__category__ = 'interface'
__only_for__ = 'gtk'


import mpd


DefaultConfig = {
    'host': "192.168.1.15",                     # LecteurAudio Host name
    'port': 6600,                               # LecteurAudio MPD server port
    'rsync_user': 'pi',                         # LecteurAudio rsync user
    'rsync_root_folder': '/var/lib/mpd/music/Podcasts/', # LecteurAudio rsync root
    'mpd_prefix': 'Podcasts' # LecteurAudio mpd music subfolder for podcasts
}

class MPDProxy:
    """Wrapper for mpd.MPDClient, reconnecting before doing anything."""

    def __init__(self, host="localhost", port=6600, timeout=10):
        self.client = mpd.MPDClient()
        self.host = host
        self.port = port

        self.client.timeout = timeout
        logger.info("connecting to %s:%i" % (host, port))
        self.connect(host, port)

    def __getattr__(self, name):
        tocall = getattr(self.client, name)
        if hasattr(tocall, '__call__'):
            return self._call_with_reconnect(getattr(self.client, name))
        else:
            return tocall

    def connect(self, host, port):
        self.client.connect(host, port)

    def _call_with_reconnect(self, func):
        def wrapper(*args, **kwargs):
            try:
                return func(*args, **kwargs)
            except mpd.ConnectionError:
                self.connect(self.host, self.port)
                return func(*args, **kwargs)
        return wrapper


class SyncToLa:
    """Implements actual syncing with device"""
    def __init__(self, gpodder, config, logger):
        self.gpodder = gpodder
        self.config = config
        self.logger = logger
        self.client = None

    def do_sync(self, episodes=None):
        """Entry point for syncing. give episodes otherwise everything is synced"""
        @util.run_in_background
        def sync_thread_fun():

            if not self.logger:
                logger.error("SyncToLa logger is not initialized")
                return

            self._init_channels()

            if not self._init_mpd():
                return

            if not self._check_key_loaded():
                return

            played_on_device = self._get_played_on_device()
            self._save_played_on_device(played_on_device)
            played_here = self._get_played_here()
            self.logger.debug("played here: %r" % [e.title for e in played_here])

            if episodes is None:
                self._rsync_to_device(played_here)
                self._put_played_on_device(played_here)
                self.logger.show_message_done_played()
                self._rsync_to_device()
            else:
                played_here = filter(self._is_played_here, episodes)
                self._rsync_to_device(episodes)
                self._put_played_on_device(played_here)

            self.logger.show_message_done()

    def _init_mpd(self):
        """Actual creation of mpd client and connection to device's mpd"""
        try:
            self.logger.set_status("gtk-network", "Connecting to MPD server")
            self.client = MPDProxy(self.config.host, self.config.port)
            self.logger.info("connected to mpd server at %s:%i version %s" % (self.config.host, self.config.port, self.client.mpd_version))
            return True
        except Exception as e:
            self.logger.set_status("gtk-dialog-error", "%s" % e)
            self.logger.error(traceback.format_exc())
            return False

    def _init_channels(self):
        self.channels = self.gpodder.model.get_podcasts()

    def _check_key_loaded(self):
        self.logger.set_status("gtk-dialog-authentication", "Checking for SSH key...")
        try:
            subprocess.check_call("ssh-add -l", shell=True)
            return True
        except CalledProcessError as e:
            self.logger.set_status("gtk-dialog-error", "No SSH key loaded: run ssh-add first")
            return False

    def _run_and_log(self, cmd):
        """Utility: get cmd's output in real time and log it through logger"""
        p = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )

        for line in iter(p.stdout.readline, b''):
            util.idle_add(self.logger._log,"\t"+line.rstrip())
        p.stdout.close()
        status = p.wait()
        if status != 0:
            self.logger.error("%r exited with code %i" % (cmd, status))
        return (status == 0)

    def _get_played_on_device(self):
        """Grab mpd stickers (= played episodes on device)"""
        self.logger.set_status("gtk-go-down", "Getting played on Device")

        stickers = self.client.sticker_find("song", "", "played")
        logger.debug("D: stickers: %r" % stickers)

        def episode_by_dir_filename(channels, dir, filename):
            for channel in channels:
                if channel.download_folder == dir:
                    for episode in channel.get_all_episodes():
                        if episode.download_filename == filename:
                            return episode
            return None

        ret = []
        for s in stickers:
            if s['file'].startswith(self.config.mpd_prefix + "/"):
                f = s['file'][len(self.config.mpd_prefix + "/"):]
                (dir,filename) = f.split("/")
                played = int(s['sticker'].split("=")[1])
                episode = episode_by_dir_filename(self.channels, dir, filename)
                if episode is None:
                    self.logger.debug("Not found episode %s" % s['file'])
                else:
                    self.logger.debug("Found matching episode: %i %i" % (episode.id,played))
                ret.append( (episode,played) )
        return ret

    def _get_played_here(self):
        """Get played episodes played from gPodder

           (= played episodes on computer)
        """
        ret = []
        for current_channel in self.channels:
            for episode in current_channel.get_all_episodes():
                if self._is_played_here(episode):
                    ret.append(episode)
        return ret

    def _is_played_here(self, episode):
        """Utility: is the episode played but not finished"""
        return episode.was_downloaded(and_exists=True) \
            and not episode.is_finished() \
            and episode.current_position > 0

    def _save_played_on_device(self, played_on_device):
        """Write played episodes in gPodder's database"""
        self.logger.set_status("gtk-go-down", "Saving played on Device")
        now = time.time()
        for (episode, played) in played_on_device:
            if played > episode.current_position:
                self.logger.debug("played on device: %i > %i" % (played, episode.current_position))
                start = episode.current_position
                episode.current_position = played
                episode.current_position_updated = now
                episode.mark(is_played=True)
                episode.save()
                self.gpodder.episode_list_status_changed([episode])
                self.gpodder.mygpo_client.on_playback_full(episode, start, played, episode.total_time)

    def _put_played_on_device(self, played_episodes):
        """Write sticker to mpd for each played_episode"""
        self.logger.set_status("gtk-go-up", "Setting played on Device")
        for episode in played_episodes:
            uri = "%s/%s/%s" % (
                    self.config.mpd_prefix,
                    episode.channel.download_folder,
                    episode.download_filename)
            self.logger.debug( "played %s" % uri )
            self.client.sticker_set("song", uri, "played", episode.current_position)

    def _rsync_to_device(self, episodes=None):
        """Actual call to rsync for episodes transfer to device.

        If episodes is given, transfer each episode in turn.
        Else, sync all folders (including deletion on device).
        """
        self.logger.set_status("gtk-go-up", "Uploading Episodes on Device")

        def run_and_update(cmd):
            self.logger.info("sync episode %r" % cmd)
            cmd_ok = self._run_and_log(cmd)
            if cmd_ok:
                self.client.update()
            return cmd_ok

        if episodes is not None:
            for e in episodes:
                file = e.local_filename(create=False)
                folder = e.channel.download_folder
                uri = self.config.mpd_prefix + "/" + folder + "/" + e.download_filename
                exists = self.client.find("file", uri)
                if exists:
                    self.logger.info("Episode %s already on LecteurAudio, no need to rsync" % uri)
                else:
                    cmd = [ "rsync", "-vtus", "--progress", "--partial", file, self._rsync_dest() + folder + "/" ]
                    return run_and_update(cmd)
        else:
            cmd = [ "rsync", "-rPvtus", "--delete", gpodder.downloads + "/", self._rsync_dest() ]
            return run_and_update(cmd)

    def _rsync_dest(self):
        """Utility: compute target to rsync to based on config"""
        return self.config.rsync_user + "@" + self.config.host + \
                ":" + self.config.rsync_root_folder


class gLogger:
    """Graphical logger for SyncToLa.

    - debug/info/error/_log
    - set_status
    - show_message_done/show_message_done_played
    """
    def __init__(self, notification, status_log, status_icon, status_label):
        self.notification = notification
        self.status_log = status_log
        self.status_icon = status_icon
        self.status_label = status_label

    def _log(self, message):
        """Append message to graphical log"""
        if self.status_log is not None:
            self.status_log.insert(self.status_log.get_end_iter(), message + "\n")

    def info(self, message):
        logger.info(message)
        util.idle_add(self._log, "I: "+message)

    def debug(self, message):
        logger.debug(message)

    def error(self, message):
        logger.error(message)
        util.idle_add(self._log, "E: " + message)

    def set_status(self, icon, message):
        def change():
            self.status_icon.set_property("icon-name", icon)
            self.status_label.set_text(message)
            self._log("------ " + message + " -------")
        util.idle_add(change)

    def show_message_done_played(self):
        title = _('Mpd Synchronization in progress')
        message = _('You can already resume playback on LecteurAudio')
        self.notification(message, title)
        self.info("%s\n%s" % (title,message))

    def show_message_done(self):
        title = _('Synchronization finished')
        message = _('LecteurAudio is fully loaded with new episodes')
        self.notification(message, title)
        self.info("%s\n%s" % (title,message))
        self.set_status("gtk-about", title)


class gPodderExtension:
    """gPodder extension for syncing to Lecteur Audio."""
    def __init__(self, container):
        self.container = container
        self.gpodder = None
        self.sync = None
        self.config = self.container.config
        self.window = None

    def on_ui_object_available(self, name, ui_object):
        """
        Called by gPodder when ui is ready
        """
        if name == 'gpodder-gtk':
            self._init_ui(ui_object)

    def on_episodes_context_menu(self, episodes):
        """
        Called by gPodder when user right-clicks on episodes
        """
        if not self.gpodder:
            return None

        # check if downloaded
        if not any(e.file_exists() for e in episodes):
            return None

        return [("Sync to LecteurAudio", self.on_sync_episodes_to_device_la_activate)]

    def _init_ui(self, gpodder):
        """
        - inject Sync to LecteurAudio menu item
        - call _init_window()
        """
        self.gpodder = gpodder
        self.gpodder_config = gpodder.config

        sync_action = gtk.Action("SyncToLaExt", "Sync To LecteurAudio", __description__, "gtk-refresh")
        sync_action.connect('activate', self.on_sync_to_device_la_activate)
        gpodder.actiongroup1.add_action(sync_action)

        uimanager = gpodder.uimanager1

        merge_id = uimanager.new_merge_id()
        uimanager.add_ui(merge_id, "ui/mainMenu/menuExtras", "test", "SyncToLaExt", gtk.UI_MANAGER_MENUITEM, False)


    def _init_window(self):
        """Create the sync window and sync client"""
        dirname = os.path.dirname(__file__)
        file_ui = dirname+"/sync_to_la.ui"
        logger.debug("loading ui from %s" % file_ui)
        b = gtk.Builder()
        b.add_from_file(file_ui)

        self.window = b.get_object("syncWindow")
        self.window.set_position(gtk.WIN_POS_CENTER_ON_PARENT)
        self.window.set_transient_for(self.gpodder.main_window)
        self.gpodder_config.register_defaults({
            'ui': {
                'gtk': {
                    'state': {
                        'sync_to_la_window':  {
                            'width': 700,
                            'height': 500,
                            'x': -1, 'y': -1,
                            'maximized': False
                        }
                    }
                }
            }
        })
        self.gpodder_config.connect_gtk_window(self.window, 'sync_to_la_window', True)

        status_icon = b.get_object("syncWindowsStatusImage")
        status_label = b.get_object("syncWindowStatusLabel")

        log = b.get_object("syncWindowStatusLog")
        self.status_log = log.get_buffer()
        self.status_log.insert(self.status_log.get_end_iter(), "Initialized Interface\n")
        self.status_scroll = b.get_object("syncWindowStatusScroll")
        log.connect('size-allocate', self.on_status_log_changed)

        b.get_object("syncWindowClose").connect("clicked", self.on_sync_close_window)

        self.g_logger = gLogger(self.notification, self.status_log, status_icon, status_label)
        self.sync = SyncToLa(self.gpodder, self.config, self.g_logger)


    def on_sync_to_device_la_activate(self, widget):
        """Handler for "Sync To LecteurAudio" menu item"""
        if self.window is None:
            self._init_window()
        self.window.show()
        self.sync.do_sync()


    def on_sync_episodes_to_device_la_activate(self, episodes):
        """Handler for "Sync To LecteurAudio" Episodes context-menu"""
        print("Sync episodes %r" % episodes)
        if self.window is None:
            self._init_window()
        self.sync.do_sync(episodes)

    def on_sync_close_window(self, _):
        """Handler for "Close" button in sync window"""
        self.window.hide()

    def on_status_log_changed(self, _a, _b):
        """Scroll to bottom in sync window log"""
        adj = self.status_scroll.get_vadjustment()
        adj.set_value( adj.upper - adj.page_size )

    def notification(self, message, title):
        """Wrapper for main.py's GTK notification"""
        self.gpodder.notification(message, title, widget=self.gpodder.main_window)
