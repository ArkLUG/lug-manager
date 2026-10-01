-- Whether this app created the event's Discord thread (1) or the user linked
-- an existing thread they chose (0). Cancelling/converting an event deletes
-- its thread only when the app owns it - previously a pre-existing thread the
-- user had picked was deleted along with all of its history. Existing rows
-- default to 1 (the old behavior) since ownership wasn't recorded before.
ALTER TABLE lug_events ADD COLUMN discord_thread_owned INTEGER NOT NULL DEFAULT 1;
