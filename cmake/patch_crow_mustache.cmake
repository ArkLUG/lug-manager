# Crow's mustache treats "" as truthy, so {{#field}}...{{/field}} renders its
# block (and {{^field}} hides its fallback) for empty strings. Templates here
# use string fields as sections everywhere ({{#location}}, {{#flash}}...), so
# make empty strings falsy, like the JavaScript mustache implementations.
# Idempotent: run as FetchContent's PATCH_COMMAND from the Crow source dir.
set(f "include/crow/mustache.h")
file(READ "${f}" src)
if(src MATCHES "LUG_EMPTY_STRING_FALSY")
  return()
endif()

set(open_old "                                case json::type::Number:
                                case json::type::String:
                                case json::type::Object:
                                case json::type::True:
                                    stack.push_back(&ctx);
                                    break;")
set(open_new "                                case json::type::String: // LUG_EMPTY_STRING_FALSY
                                    if (ctx.s.empty())
                                        current = action.pos;
                                    else
                                        stack.push_back(&ctx);
                                    break;
                                case json::type::Number:
                                case json::type::Object:
                                case json::type::True:
                                    stack.push_back(&ctx);
                                    break;")
set(else_old "                                case json::type::False:
                                case json::type::Null:
                                    stack.emplace_back(&nullContext);
                                    break;")
set(else_new "                                case json::type::String:
                                    if (ctx.s.empty())
                                        stack.emplace_back(&nullContext);
                                    else
                                        current = action.pos;
                                    break;
                                case json::type::False:
                                case json::type::Null:
                                    stack.emplace_back(&nullContext);
                                    break;")

string(FIND "${src}" "${open_old}" p1)
string(FIND "${src}" "${else_old}" p2)
if(p1 EQUAL -1 OR p2 EQUAL -1)
  message(FATAL_ERROR "patch_crow_mustache: anchors not found - Crow version changed?")
endif()
string(REPLACE "${open_old}" "${open_new}" src "${src}")
string(REPLACE "${else_old}" "${else_new}" src "${src}")
file(WRITE "${f}" "${src}")
