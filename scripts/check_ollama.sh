#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:11434}"
MODEL="${2:-qwen2.5:7b}"

normalize_tags_url() {
  local url="$1"
  url="${url%/}"
  if [[ "$url" == */api ]]; then
    printf '%s/tags\n' "$url"
  else
    printf '%s/api/tags\n' "$url"
  fi
}

normalize_generate_url() {
  local url="$1"
  url="${url%/}"
  if [[ "$url" == */api ]]; then
    printf '%s/generate\n' "$url"
  else
    printf '%s/api/generate\n' "$url"
  fi
}

TAGS_URL="$(normalize_tags_url "$BASE_URL")"
GENERATE_URL="$(normalize_generate_url "$BASE_URL")"

echo "[*] Checking Ollama tags endpoint: $TAGS_URL"
curl -fsS "$TAGS_URL" | sed 's/{/\n{/g'

echo
echo "[*] Sending tiny generate request to: $GENERATE_URL"
curl -fsS "$GENERATE_URL" \
  -H 'Content-Type: application/json' \
  -d "{\"model\":\"$MODEL\",\"prompt\":\"Say ok.\",\"stream\":false}"
echo
