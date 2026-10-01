#!/usr/bin/env python3
"""Fill a freshly migrated LUG Manager database with a fictional LUG for the
public demo ("Brickton LUG"). Every person, place and number here is made up.

    python3 scripts/demo/seed.py <lug.db> <uploads-dir>

Dates are relative to today so the demo always looks current. Prints the
three demo session tokens (member / moderator / admin) as JSON on stdout.
"""
import datetime as dt
import hashlib
import json
import os
import random
import sqlite3
import struct
import sys
import uuid
import zlib

DB, UPLOADS = sys.argv[1], sys.argv[2]
rng = random.Random(42)          # deterministic demo
TODAY = dt.date.today()
NOW = dt.datetime.now().replace(minute=0, second=0, microsecond=0)


def d(days):
    return (TODAY + dt.timedelta(days=days)).isoformat()


def t(days, hour, minute=0):
    x = dt.datetime.combine(TODAY + dt.timedelta(days=days), dt.time(hour, minute))
    return x.strftime("%Y-%m-%dT%H:%M:%S")


def ts(days, hour=12):
    x = dt.datetime.combine(TODAY + dt.timedelta(days=days), dt.time(hour, 0))
    return x.strftime("%Y-%m-%d %H:%M:%S")


def fake_snowflake():
    return str(rng.randint(10**17, 10**18 - 1))


# ── Little PNG generator: a few bricks with studs on a coloured baseplate ──
def png(width, height, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y * width * 3:(y + 1) * width * 3]) for y in range(height))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


COLOURS = [(201, 26, 9), (0, 85, 191), (242, 205, 55), (35, 120, 65), (254, 138, 24), (255, 255, 255), (5, 19, 29)]


def brick_picture(seed):
    r = random.Random(seed)
    w, h = 360, 240
    base = r.choice([(35, 120, 65), (160, 165, 169), (0, 85, 191), (88, 42, 18)])
    px = bytearray(base * (w * h))

    def rect(x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(h, y1)):
            row = y * w * 3
            for x in range(max(0, x0), min(w, x1)):
                px[row + x * 3:row + x * 3 + 3] = bytes(c)

    def shade(c, f):
        return tuple(max(0, min(255, int(v * f))) for v in c)

    # baseplate studs
    for sy in range(10, h, 24):
        for sx in range(10, w, 24):
            rect(sx, sy, sx + 10, sy + 10, shade(base, 1.15))
    # a stack of bricks
    for _ in range(r.randint(7, 12)):
        c = r.choice(COLOURS)
        bw = r.choice([48, 72, 96])
        bx, by = r.randint(0, w - bw), r.randint(30, h - 40)
        rect(bx, by, bx + bw, by + 28, c)
        rect(bx, by + 24, bx + bw, by + 28, shade(c, 0.7))
        for sx in range(bx + 6, bx + bw - 10, 24):
            rect(sx, by - 6, sx + 14, by, shade(c, 1.1))
    return png(w, h, px)


def save_picture(seed):
    os.makedirs(UPLOADS, exist_ok=True)
    name = hashlib.sha256(f"demo-{seed}".encode()).hexdigest()[:32] + ".png"
    with open(os.path.join(UPLOADS, name), "wb") as f:
        f.write(brick_picture(seed))
    return name


con = sqlite3.connect(DB)
con.execute("PRAGMA foreign_keys=ON")
q = con.execute


def ins(sql, *args):
    return q(sql + " RETURNING id", args).fetchone()[0]


# ── Settings ──
settings = {
    "lug_name": "Brickton LUG",
    "lug_timezone": "America/Chicago",
    "ical_calendar_name": "Brickton LUG",
    "setup_completed": "1",
    "setup_features_done": "1",
    "feature_digest": "1",
    "public_shows_enabled": "1",
    "public_shows_title": "Brickton LUG - upcoming shows",
    "public_shows_intro": "Come see our LEGO train layouts, castles and city displays! Everything is free unless noted.",
    "dues_reminder_days": "14",
    "backup_enabled": "0",
}
for k, v in settings.items():
    q("INSERT OR REPLACE INTO lug_settings (key, value) VALUES (?, ?)", (k, v))

# ── Chapters ──
north = ins("INSERT INTO chapters (name, shorthand, description, discord_announcement_channel_id) VALUES (?,?,?,?)",
            "North Brickton", "NB", "Meets at the North Branch library, second Tuesday of the month.", "")
river = ins("INSERT INTO chapters (name, shorthand, description, discord_announcement_channel_id) VALUES (?,?,?,?)",
            "Riverside", "RS", "Weekend builders around the Riverside community center.", "")

# ── Members ──
PEOPLE = [
    # first, last, role, fol, chapter, chapter_role, paid
    ("Maya", "Torres", "admin", "afol", north, "lead", True),
    ("Sam", "Okafor", "moderator", "afol", river, "lead", True),
    ("Jordan", "Lee", "member", "afol", north, "member", True),
    ("Priya", "Nair", "chapter_lead", "afol", north, "lead", True),
    ("Ben", "Hartley", "member", "afol", river, "event_manager", True),
    ("Lucia", "Moreno", "member", "afol", river, "member", False),
    ("Theo", "Brandt", "member", "tfol", north, "member", True),
    ("Ava", "Brandt", "member", "kfol", north, "member", False),
    ("Marcus", "Webb", "member", "afol", None, None, True),
    ("Hana", "Sato", "member", "afol", river, "member", True),
    ("Oliver", "Grant", "member", "afol", north, "member", False),
    ("Zoe", "Kim", "member", "tfol", river, "member", True),
    ("Elias", "Fischer", "member", "afol", north, "member", True),
    ("Nina", "Patel", "member", "afol", river, "member", True),
    ("Caleb", "Owens", "member", "afol", None, None, False),
    ("Ruth", "Delgado", "member", "afol", north, "member", True),
    ("Felix", "Novak", "member", "kfol", river, "member", False),
    ("Grace", "Adeyemi", "member", "afol", north, "member", True),
    ("Leo", "Martins", "member", "afol", river, "member", True),
    ("Ivy", "Chen", "member", "afol", north, "member", False),
    ("Wes", "Carter", "member", "afol", None, None, True),
    ("Sofia", "Rossi", "member", "afol", river, "member", True),
    ("Dev", "Rao", "member", "afol", north, "member", True),
    ("Amara", "Okoye", "member", "tfol", river, "member", False),
]
members = []
for i, (first, last, role, fol, ch, ch_role, paid) in enumerate(PEOPLE):
    display = f"{first} {last[0]}."
    created = ts(-rng.randint(20, 760))
    if i < 3:
        created = ts(-700)
    mid = ins(
        "INSERT INTO members (discord_user_id, discord_username, display_name, first_name, last_name, email, role, "
        "role_source, fol_status, is_paid, paid_until, created_at, phone, city, state, sharing_email, sharing_discord) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        fake_snowflake(), f"{first.lower()}{last.lower()[:3]}", display, first, last,
        f"{first.lower()}.{last.lower()}@example.org", role, "manual", fol,
        1 if paid else 0, d(rng.randint(5, 300)) if paid else d(-rng.randint(10, 200)), created,
        f"(555) 01{i:02d}-{1000 + i * 37:04d}", "Brickton", "AR",
        rng.choice(["none", "verified", "all"]), "all")
    members.append(mid)
    if ch:
        q("INSERT INTO chapter_members (member_id, chapter_id, chapter_role) VALUES (?,?,?)", (mid, ch, ch_role))
ADMIN, MOD, JORDAN, PRIYA, BEN = members[0], members[1], members[2], members[3], members[4]
# Young members: guardians + consent
q("UPDATE members SET guardian_name='Karin Brandt', guardian_phone='(555) 010-2000', guardian_email='karin@example.org', "
  "consent_on_file=1, consent_date=?, photo_release=1 WHERE id IN (?,?)", (d(-200), members[6], members[7]))
q("UPDATE members SET guardian_name='Pavel Novak', consent_on_file=0, photo_release=0 WHERE id=?", (members[16],))
# An email-only member (no Discord)
q("UPDATE members SET discord_user_id=NULL, discord_username='' WHERE id=?", (members[20],))
q("UPDATE members SET is_treasurer=1 WHERE id=?", (MOD,))
# A Recognized LEGO Fan Community (LEGO Fan CoLab) with Ben as Community Ambassador
q("INSERT OR REPLACE INTO lug_settings (key, value) VALUES ('fan_colab_recognized', '1'), ('community_ambassador_id', ?)", (str(BEN),))

# ── Perk levels (this year) ──
year = TODAY.year
for name, m, e, paid, desc, order in [
        ("Bronze Brick", 3, 0, 0, "Show up a few times - welcome aboard!", 1),
        ("Silver Brick", 6, 1, 1, "Regular builder: 6 meetings and a show.", 2),
        ("Gold Brick", 9, 3, 1, "Pillar of the LUG.", 3)]:
    q("INSERT INTO perk_levels (name, meeting_attendance_required, event_attendance_required, requires_paid_dues, "
      "sort_order, year, description) VALUES (?,?,?,?,?,?,?)", (name, m, e, paid, order, year, desc))

# ── Meetings: monthly for the past ~10 months, and the next three ──
LOCS = ["Brickton Public Library", "Riverside Community Center", "North Branch Library"]
meeting_ids = []
for k in range(-10, 3):
    start_day = k * 30 + 3
    loc = LOCS[k % 3]
    virtual = k == -4
    status = "completed" if k < 0 else "scheduled"
    scope, ch = ("chapter", north) if k % 3 == 2 else (("chapter", river) if k % 3 == 1 else ("lug_wide", None))
    mid = ins("INSERT INTO meetings (title, description, location, start_time, end_time, status, ical_uid, scope, "
              "chapter_id, is_virtual, suppress_discord, suppress_calendar) VALUES (?,?,?,?,?,?,?,?,?,?,1,1)",
              ["Build Night", "Monthly Meeting", "Swap & Sort"][k % 3] + ("" if k < 0 else ""),
              "Bring a work-in-progress and your spare parts. **Snacks provided.**",
              "Virtual (Discord)" if virtual else loc, t(start_day, 19), t(start_day, 21), status,
              str(uuid.UUID(int=rng.getrandbits(128))), scope, ch, 1 if virtual else 0)
    meeting_ids.append((mid, k))
    if k < 0:
        for m in rng.sample(members, rng.randint(7, 16)):
            q("INSERT OR IGNORE INTO attendance (member_id, entity_type, entity_id, checked_in_at, is_virtual) "
              "VALUES (?, 'meeting', ?, ?, ?)", (m, mid, t(start_day, 19, 10).replace("T", " "), 1 if virtual else 0))
# A recurring schedule
q("INSERT INTO meeting_series (title, description, location, scope, start_hm, end_hm, rule, weekday, nth, starts_on, "
  "suppress_discord, suppress_calendar, active, created_by) VALUES ('Build Night', 'Second Tuesday build night', "
  "'North Branch Library', 'chapter', '19:00', '21:00', 'monthly', 2, 2, ?, 1, 1, 1, ?)", (d(-120), ADMIN))
q("UPDATE meeting_series SET chapter_id=? WHERE title='Build Night'", (north,))

# ── Events ──
def event(title, start, days, loc, status="confirmed", scope="lug_wide", fee="", maxa=0, desc="", kids=0, teens=0,
          adults=0, notes="", deadline="", lead=None):
    eid = ins("INSERT INTO lug_events (title, description, location, start_time, end_time, status, ical_uid, scope, "
              "entrance_fee, max_attendees, public_kids, public_teens, public_adults, notes, signup_deadline, "
              "event_lead_id, suppress_discord, suppress_calendar) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,1,1)",
              title, desc, loc, t(start, 9), t(start + days - 1, 17), status, str(uuid.UUID(int=rng.getrandbits(128))),
              scope, fee, maxa, kids, teens, adults, notes, deadline, lead)
    for n in range(days):
        q("INSERT INTO event_days (event_id, day_date, day_number) VALUES (?,?,?)", (eid, d(start + n), n + 1))
    return eid

past_fest = event("Brickton Brick Fest", -95, 2, "Brickton Expo Hall", fee="$5 (kids free)",
                  desc="Our biggest show of the year: **train layouts, a castle, and a moon base**.",
                  kids=412, teens=96, adults=538, lead=BEN,
                  notes="Great turnout. The moon base was the hit of the show.")
past_lib = event("Library Summer Display", -40, 1, "Brickton Public Library", fee="Free",
                 desc="A week-long display case plus a build table for kids.", kids=120, teens=14, adults=85, lead=PRIYA)
past_mall = event("Riverside Mall Pop-up", -12, 1, "Riverside Mall", fee="Free", kids=64, teens=11, adults=90)
for eid in (past_fest, past_lib, past_mall):
    days = [r[0] for r in q("SELECT id FROM event_days WHERE event_id=?", (eid,))]
    for day in days:
        for m in rng.sample(members, rng.randint(6, 12)):
            q("INSERT OR IGNORE INTO event_day_attendance (event_day_id, member_id) VALUES (?,?)", (day, m))

up_fest = event("Brickton Brick Fest", 24, 2, "Brickton Expo Hall", fee="$5 (kids free)", maxa=12,
                desc="Two days of LUG displays. Sign up to bring a MOC and grab a volunteer shift!",
                deadline=t(17, 23, 59), lead=BEN)
up_lib = event("Library Display Case", 9, 1, "North Branch Library", fee="Free",
               desc="Small display in the lobby case all month.", scope="chapter")
q("UPDATE lug_events SET chapter_id=? WHERE id=?", (north, up_lib))
up_train = event("Holiday Train Show", 52, 1, "Brickton Rail Museum", status="tentative", fee="$3",
                 desc="Date to be confirmed with the museum.")

# Visitors who tapped "I plan to come" on the public shows page
q("UPDATE lug_events SET public_interest=37 WHERE id=?", (up_fest,))
q("UPDATE lug_events SET public_interest=8 WHERE id=?", (up_lib,))

# RSVPs (the fest is full, two on the waitlist)
for i, m in enumerate(members[:14]):
    q("INSERT INTO event_rsvps (event_id, member_id, status, created_at) VALUES (?,?,?,?)",
      (up_fest, m, "going" if i < 12 else "waitlist", ts(-10 + i)))
for m in members[2:8]:
    q("INSERT INTO event_rsvps (event_id, member_id, status) VALUES (?,?, 'going')", (up_lib, m))

# Volunteer shifts
for title, day, h1, h2, slots in [("Set-up", 24, 7, 9, 4), ("Booth - morning", 24, 9, 13, 3),
                                  ("Booth - afternoon", 24, 13, 17, 3), ("Tear-down", 25, 17, 19, 5)]:
    sid = ins("INSERT INTO event_shifts (event_id, title, starts_at, ends_at, slots) VALUES (?,?,?,?,?)",
              up_fest, title, t(day, h1)[:16], t(day, h2)[:16], slots)
    for m in rng.sample(members[:12], rng.randint(1, slots)):
        q("INSERT OR IGNORE INTO event_shift_signups (shift_id, member_id) VALUES (?,?)", (sid, m))

# Display requests
for m, title, w, dpt, power, status, table in [
        (JORDAN, "Brickton Central Station", 96, 48, 1, "approved", "Table 3"),
        (PRIYA, "Medieval River Castle", 48, 48, 0, "approved", "Table 1"),
        (members[9], "Moon Base Alpha", 64, 32, 1, "pending", ""),
        (members[12], "Steam Locomotive Collection", 72, 24, 0, "pending", "")]:
    q("INSERT INTO event_display_requests (event_id, member_id, title, description, width_in, depth_in, needs_power, "
      "status, table_assignment) VALUES (?,?,?,?,?,?,?,?,?)", (up_fest, m, title, "", w, dpt, power, status, table))

# Event photos (past shows)
for n, (eid, caption) in enumerate([(past_fest, "The moon base"), (past_fest, "Train layout, day 2"),
                                     (past_fest, "Kids' build table"), (past_lib, "Library case"),
                                     (past_mall, "Pop-up table")]):
    q("INSERT INTO event_photos (event_id, member_id, file, caption) VALUES (?,?,?,?)",
      (eid, members[n + 2], save_picture(f"photo{n}"), caption))

# ── Dues ──
for m in members:
    paid = q("SELECT is_paid, paid_until FROM members WHERE id=?", (m,)).fetchone()
    if paid[0]:
        q("INSERT INTO dues_payments (member_id, paid_on, amount_cents, method, covers_until, recorded_by) "
          "VALUES (?,?,?,?,?,?)", (m, d(-rng.randint(20, 300)), 2500, rng.choice(["cash", "card", "PayPal"]), paid[1], MOD))

# ── Inventory: locations (who looks after them), items spread across them, loans ──
unit = ins("INSERT INTO storage_locations (name, kind, address, notes, keeper_id) VALUES (?,?,?,?,?)",
           "Storage unit 14", "storage", "Brickton Self Storage, 400 Mill Rd", "Gate code is in the LUG Discord pins.", ADMIN)
trailer = ins("INSERT INTO storage_locations (name, kind, address, notes, keeper_id) VALUES (?,?,?,?,?)",
              "Show trailer", "trailer", "Parked at Sam's", "Hitch lock key on the LUG keyring.", MOD)
garage = ins("INSERT INTO storage_locations (name, kind, address, keeper_id) VALUES (?,?,?,?)",
             "Maya's garage", "home", "Maya T.'s house", ADMIN)
items = {}
for name, cat, placements in [("6ft folding table", "Furniture", [(unit, 4), (trailer, 4)]),
                              ("48x48 grey baseplate", "Display", [(unit, 16), (trailer, 12)]),
                              ("Acrylic display case", "Display", [(garage, 3)]),
                              ("Brickton LUG banner", "Signage", [(trailer, 2)]),
                              ("Extension cord (25ft)", "Electrical", [(trailer, 6)])]:
    total = sum(n for _, n in placements)
    items[name] = ins("INSERT INTO inventory_items (name, category, quantity) VALUES (?,?,?)", name, cat, total)
    for loc, n in placements:
        q("INSERT INTO inventory_stock (item_id, location_id, quantity) VALUES (?,?,?)", (items[name], loc, n))
# Two loans: taken from a location (so stock there is lower), one overdue
q("UPDATE inventory_items SET quantity = quantity + 12 WHERE id=?", (items["48x48 grey baseplate"],))
q("INSERT INTO inventory_loans (item_id, member_id, quantity, due_on, notes, checked_out_by, from_location_id) VALUES (?,?,?,?,?,?,?)",
  (items["48x48 grey baseplate"], JORDAN, 12, d(20), "For Brick Fest layout", ADMIN, unit))
q("UPDATE inventory_items SET quantity = quantity + 1 WHERE id=?", (items["Acrylic display case"],))
q("INSERT INTO inventory_loans (item_id, member_id, quantity, due_on, notes, checked_out_by, from_location_id) VALUES (?,?,?,?,?,?,?)",
  (items["Acrylic display case"], PRIYA, 1, d(-3), "Library display", ADMIN, garage))

# Condition, photos and a pack list for Brick Fest
q("UPDATE inventory_items SET condition='needs_repair', condition_note='One leg wobbles - needs a new bolt' WHERE id=?",
  (items["6ft folding table"],))
q("UPDATE inventory_items SET photo_file=? WHERE id=?", (save_picture("case"), items["Acrylic display case"]))
q("UPDATE inventory_items SET photo_file=? WHERE id=?", (save_picture("banner"), items["Brickton LUG banner"]))
for name, n, packed, note in [("6ft folding table", 6, 1, ""), ("48x48 grey baseplate", 24, 1, "Train layout"),
                              ("Brickton LUG banner", 1, 0, ""), ("Extension cord (25ft)", 4, 0, "Moon base lights")]:
    q("INSERT INTO inventory_pack (event_id, item_id, quantity, packed, note) VALUES (?,?,?,?,?)",
      (up_fest, items[name], n, packed, note))

# ── Treasury ──
for days, kind, cat, cents, desc, eid in [(-300, "income", "Opening balance", 84000, "Carried over", None),
                                          (-96, "expense", "Table fee", 15000, "Expo Hall booth", past_fest),
                                          (-95, "income", "Sponsorship", 25000, "Brickton Hobby Shop", past_fest),
                                          (-60, "expense", "Supplies", 4599, "Baseplates and storage bins", None),
                                          (-41, "expense", "Supplies", 2350, "Display case risers", past_lib),
                                          (-20, "income", "Donation", 5000, "Thank-you from the library", None)]:
    q("INSERT INTO treasury_entries (entry_on, kind, category, amount_cents, description, event_id, recorded_by) "
      "VALUES (?,?,?,?,?,?,?)", (d(days), kind, cat, cents, desc, eid, MOD))

# ── Build challenges ──
open_ch = ins("INSERT INTO challenges (title, description, starts_on, ends_on, created_by) VALUES (?,?,?,?,?)",
              "Microscale Castle", "Build a castle that fits on a 16x16 plate.", d(-10), d(6), ADMIN)
done_ch = ins("INSERT INTO challenges (title, description, starts_on, ends_on, announced, created_by) VALUES (?,?,?,?,1,?)",
              "Spaceship in 100 pieces", "Any style, 100 pieces or fewer.", d(-70), d(-50), ADMIN)
entries = {}
for n, (ch, m, title) in enumerate([(open_ch, JORDAN, "Tiny Keep"), (open_ch, members[9], "Cliffside Fortress"),
                                    (open_ch, members[12], "Island Watchtower"),
                                    (done_ch, members[5], "Star Courier"), (done_ch, members[10], "Red Falcon"),
                                    (done_ch, PRIYA, "Mining Barge")]):
    entries[title] = ins("INSERT INTO challenge_entries (challenge_id, member_id, title, file) VALUES (?,?,?,?)",
                         ch, m, title, save_picture(f"entry{n}"))
for voter, entry in [(members[3], "Red Falcon"), (members[6], "Red Falcon"), (members[13], "Star Courier"),
                     (members[14], "Red Falcon"), (members[15], "Mining Barge"), (members[17], "Cliffside Fortress")]:
    ch = q("SELECT challenge_id FROM challenge_entries WHERE id=?", (entries[entry],)).fetchone()[0]
    q("INSERT INTO challenge_votes (challenge_id, entry_id, member_id) VALUES (?,?,?)", (ch, entries[entry], voter))

# ── A little audit history ──
for days, actor, name, action, etype, ename, det in [
        (-30, ADMIN, "Maya T.", "event.create", "event", "Brickton Brick Fest", "Created event"),
        (-20, MOD, "Sam O.", "treasury.add", "treasury", "Donation", "+$50.00"),
        (-5, ADMIN, "Maya T.", "inventory.checkout", "inventory", "48x48 grey baseplate", "12 to Jordan L.")]:
    q("INSERT INTO audit_log (actor_id, actor_name, action, entity_type, entity_name, details, ip_address, created_at) "
      "VALUES (?,?,?,?,?,?, '203.0.113.7', ?)", (actor, name, action, etype, ename, det, ts(days)))

# ── Demo sessions: one per viewpoint ──
tokens = {}
for role, mid in [("member", JORDAN), ("moderator", MOD), ("admin", ADMIN)]:
    raw = hashlib.sha256(f"brickton-demo-{role}".encode()).hexdigest()
    lug_role = q("SELECT role FROM members WHERE id=?", (mid,)).fetchone()[0]
    q("INSERT INTO sessions (token, member_id, role, expires_at, token_is_hash, user_agent) VALUES (?,?,?,?,1,?)",
      (hashlib.sha256(raw.encode()).hexdigest(), mid, lug_role, ts(30), "Demo browser"))
    tokens[role] = raw

con.commit()
print(json.dumps(tokens))
