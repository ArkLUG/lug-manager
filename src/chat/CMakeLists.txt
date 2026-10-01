# src/chat: The chat-service layer: the Provider interface, ChatHub (decides what to post and keeps posts in step), message templates and formatting. Each service (Discord, later Slack, Matrix...) is a Provider under integrations/.
target_sources(lug_core PRIVATE
  ChatHub.cpp
  ChatHub.hpp
  Format.hpp
  Provider.hpp
  Templates.hpp
)
