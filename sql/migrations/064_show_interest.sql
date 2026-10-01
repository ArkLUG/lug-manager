-- "I plan to come" clicks on the public shows page (anonymous count).
ALTER TABLE lug_events ADD COLUMN public_interest INTEGER NOT NULL DEFAULT 0;
