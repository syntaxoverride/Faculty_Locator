#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DASHBOARD_SERVICE_NAME="faculty-dashboard.service"
DASHBOARD_SERVICE_PATH="/etc/systemd/system/${DASHBOARD_SERVICE_NAME}"
MOSQUITTO_CONFIG_PATH="/etc/mosquitto/conf.d/faculty-locator.conf"

require_ubuntu() {
  if [[ ! -f /etc/os-release ]]; then
    echo "This script expects Ubuntu or another systemd-based Linux distro."
    exit 1
  fi

  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "${ID:-}" != "ubuntu" ]]; then
    echo "Warning: this script was written for Ubuntu. Detected: ${PRETTY_NAME:-unknown}"
  fi
}

require_sudo() {
  if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required."
    exit 1
  fi
}

first_ip() {
  hostname -I | awk '{print $1}'
}

dashboard_user() {
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
    echo "${SUDO_USER}"
  else
    id -un
  fi
}

install_mosquitto() {
  echo
  echo "Installing Mosquitto..."
  sudo apt-get update
  sudo apt-get install -y mosquitto mosquitto-clients ufw

  sudo tee "${MOSQUITTO_CONFIG_PATH}" >/dev/null <<'EOF'
listener 1883
allow_anonymous true
EOF

  sudo systemctl enable mosquitto
  sudo systemctl restart mosquitto
  sudo ufw allow 1883/tcp >/dev/null 2>&1 || true

  echo
  echo "Mosquitto is configured."
  echo "Broker should be reachable at: $(first_ip):1883"
  sudo systemctl --no-pager --full status mosquitto || true
}

install_dashboard() {
  local port user venv_python venv_uvicorn ip

  user="$(dashboard_user)"
  ip="$(first_ip)"

  read -r -p "Dashboard port [8000]: " port
  port="${port:-8000}"

  echo
  echo "Installing dashboard dependencies..."
  sudo apt-get update
  sudo apt-get install -y python3 python3-venv python3-pip ufw

  if [[ ! -d "${REPO_ROOT}/.venv" ]]; then
    python3 -m venv "${REPO_ROOT}/.venv"
  fi

  venv_python="${REPO_ROOT}/.venv/bin/python"
  venv_uvicorn="${REPO_ROOT}/.venv/bin/uvicorn"

  "${venv_python}" -m pip install --upgrade pip
  "${venv_python}" -m pip install -r "${REPO_ROOT}/backend/requirements.txt"

  sudo tee "${DASHBOARD_SERVICE_PATH}" >/dev/null <<EOF
[Unit]
Description=Faculty Presence Dashboard
After=network.target mosquitto.service
Requires=mosquitto.service

[Service]
Type=simple
User=${user}
WorkingDirectory=${REPO_ROOT}
Environment=PYTHONUNBUFFERED=1
ExecStart=${venv_uvicorn} backend.server:app --host 0.0.0.0 --port ${port}
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

  sudo systemctl daemon-reload
  sudo systemctl enable "${DASHBOARD_SERVICE_NAME}"
  sudo systemctl restart "${DASHBOARD_SERVICE_NAME}"
  sudo ufw allow "${port}"/tcp >/dev/null 2>&1 || true

  echo
  echo "Dashboard is configured."
  echo "Open: http://${ip}:${port}"
  sudo systemctl --no-pager --full status "${DASHBOARD_SERVICE_NAME}" || true
}

uninstall_dashboard() {
  local remove_venv

  echo
  echo "Removing dashboard service..."
  sudo systemctl stop "${DASHBOARD_SERVICE_NAME}" >/dev/null 2>&1 || true
  sudo systemctl disable "${DASHBOARD_SERVICE_NAME}" >/dev/null 2>&1 || true
  sudo rm -f "${DASHBOARD_SERVICE_PATH}"
  sudo systemctl daemon-reload

  read -r -p "Remove Python virtual environment at ${REPO_ROOT}/.venv? [y/N]: " remove_venv
  if [[ "${remove_venv}" =~ ^[Yy]$ ]]; then
    rm -rf "${REPO_ROOT}/.venv"
    echo "Removed ${REPO_ROOT}/.venv"
  fi

  echo "Dashboard uninstall complete."
}

show_menu() {
  echo
  echo "Faculty Locator VM Setup"
  echo "Repo: ${REPO_ROOT}"
  echo "1. Setup Mosquitto"
  echo "2. Setup Dashboard"
  echo "3. Uninstall Dashboard"
  echo "4. Exit"
}

main() {
  local choice

  require_ubuntu
  require_sudo

  while true; do
    show_menu
    read -r -p "Choose an option [1-4]: " choice
    case "${choice}" in
      1)
        install_mosquitto
        ;;
      2)
        install_dashboard
        ;;
      3)
        uninstall_dashboard
        ;;
      4)
        echo "Exiting."
        exit 0
        ;;
      *)
        echo "Invalid option."
        ;;
    esac
  done
}

main "$@"
