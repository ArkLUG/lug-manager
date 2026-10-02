# Crow buffers every request body in memory with no size limit, so one huge
# request (signed in or not) could exhaust the server's memory. Refuse bodies
# over LUG_MAX_REQUEST_BYTES (25 MB; uploads are capped at 10 MB): a too-big
# Content-Length is turned away as soon as the headers are in, and a body
# without one is cut off once it passes the limit. The connection is closed.
# Idempotent: run as FetchContent's PATCH_COMMAND from the Crow source dir.
set(f "include/crow/parser.h")
file(READ "${f}" src)
if(src MATCHES "LUG_MAX_REQUEST_BYTES")
  return()
endif()

set(hdr_old "            self->set_connection_parameters();

            self->process_header();
            return 0;")
set(hdr_new "            self->set_connection_parameters();
            // LUG_MAX_REQUEST_BYTES: refuse an oversized body up front
            if (self_->content_length != CROW_ULLONG_MAX && self_->content_length > 25ull * 1024 * 1024)
                return 3;   // not 1 (= "no body") or 2 (= upgrade): an error, the connection closes

            self->process_header();
            return 0;")
set(body_old "            self->req.body.insert(self->req.body.end(), at, at + length);
            return 0;")
set(body_new "            if (self->req.body.size() + length > 25ull * 1024 * 1024) return 1;   // LUG_MAX_REQUEST_BYTES
            self->req.body.insert(self->req.body.end(), at, at + length);
            return 0;")

string(FIND "${src}" "${hdr_old}" p1)
string(FIND "${src}" "${body_old}" p2)
if(p1 EQUAL -1 OR p2 EQUAL -1)
  message(FATAL_ERROR "patch_crow_body_limit: anchors not found - Crow version changed?")
endif()
string(REPLACE "${hdr_old}" "${hdr_new}" src "${src}")
string(REPLACE "${body_old}" "${body_new}" src "${src}")
file(WRITE "${f}" "${src}")
