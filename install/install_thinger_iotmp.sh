#!/bin/sh

trap 'echo "Caught: 90 seconds till SIGKILL' SIGTERM # catches systemctl stop on update

_repo="IOTMP-Linux"
_module="thinger_iotmp"
_github_api_url="api.github.com"
_user_agent="Simple User Agent 1.0"

# archtitecture to download
case `uname -m` in
  x86_64)
    _arch=`uname -m` ;;
  amd64)
    _arch="x86_64" ;;
  *arm*)
    _arch="arm" ;;
  *aarch64*)
    _arch="aarch64" ;;
esac

usage() {
    echo "usage: install_$_module.sh [-h] [-u USERNAME] [-d DEVICE] [-s SERVER] [-t TRANSPORT] [-p PREFIX] [-e SEED] [--uninstall]"
    echo
    echo "Installs and runs $_module binary"
    echo
    echo "optional arguments:"
    echo " -h, --help            show this help message and exit"
    echo " -u USERNAME, --username USERNAME"
    echo "                       Username of Thinger.io Platform to connect to"
    echo " -d DEVICE, --device DEVICE"
    echo "                       Device Id of Thinger.io Platform to connect to"
    echo " -s SERVER, --server SERVER"
    echo "                       Ip or URL where the Thinger.io Platform is installed"
    echo " -p PREFIX, --prefix PREFIX"
    echo "                       Prefix of the device id"
    echo " -t TRANSPORT, --transport  TRANSPORT"
    echo "                       Sets the protocol transport to use, default or 'websocket'"
    echo " -e SEED, -seed SEED"
    echo "                       Sets the seed for the generation of the random device credentials"
    echo " -v, --version         Version of $_module to install. Default is latest published production release"
    echo " --uninstall       Uninstalls the $_module program and removes all associated files"

    exit 0
}

INIT=
get_init_system() {
  ps | grep procd | grep -v 'grep' > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    INIT="procd"
    return
  fi

  ps -p 1 -o comm= | grep systemd | grep -v 'grep' > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    INIT="systemd"
    return
  fi
}

set_directories() {
    # Check init system first
    if [ "$INIT" == "procd" ]; then
      export bin_dir="/usr/local/bin"
      export config_dir="/etc/thinger/"
      service_dir="/etc/init.d/"
    elif [ "$INIT" == "systemd" ]; then
      # Set install directories based on user
      if [ "$UID" -eq 0 ]; then
        export bin_dir="/usr/local/bin/"
        export home_dir="$HOME"
        export config_dir="$home_dir/.thinger/"
        service_dir="/etc/systemd/system/"
        sys_user=""
      else
        export bin_dir="$HOME/.local/bin/"
        export home_dir="$HOME"
        export config_dir="$home_dir/.thinger/"
        service_dir="$HOME/.config/systemd/user/"
        sys_user="--user"

    fi
  fi
}

uninstall() {
    # remove bin, disable and remove service, remove config
    #echo "Uninstalling $_module"

    set_directories

    rm -f "$bin_dir"/"$_module"

    if [ "$INIT" == "procd" ]; then
      service "$_module" stop
      service "$_module" disable
      rm -f "$service_dir"/"$_module"
    elif [ "$INIT" == "systemd" ]; then
      systemctl $sys_user stop "$_module"
      systemctl $sys_user disable "$_module"
      rm -f "$service_dir"/"$_module".service
    fi

    rm -f "$config_dir"/iotmp.cfg

    echo "Uninstalled $_module"
    exit 0
}

# Parse options
while [ "$#" -gt 0 ]; do case $1 in
  -u | --username )
    shift; username="$1"
    ;;
  -d | --device )
    shift; device="$1"
    ;;
  -s | --server )
    shift; server="$1"
    ;;
  -t | --transport )
    shift; transport="$1"
    ;;
  -p | --prefix )
    shift; prefix="$1"
    ;;
  -e | --seed )
    shift; seed="$1"
    ;;
  -v | --version )
    shift; version="$1"
    ;;
  --uninstall )
    uninstall
    ;;
  -h | --help )
    usage
    ;;
  *)
    usage
    ;;
esac; shift; done

get_init_system
if [ -z "${INIT+x}" ]; then
  exit -1
fi

set_directories
mkdir -p $bin_dir $config_dir $service_dir

# Set SSL_CERT_DIR if exists
if [ -n "${SSL_CERT_DIR+x}" ]; then
    export certs_dir_env="Environment=SSL_CERT_DIR=$SSL_CERT_DIR"
fi

if [ -z "${version+x}" ]; then
  if type curl > /dev/null; then
    version=$(curl -s -L -H "Accept: application/vnd.github.v3+json" "https://$_github_api_url/repos/thinger-io/$_repo/releases/latest" | grep -o '"tag_name": "[^"]*' | cut -d'"' -f4)
  else
    # Check GNU wget is installed
    ls -lah $(which wget) | grep -E 'busybox|uclient' > /dev/null 2>&1
    if [ "$?" -eq 0 ]; then
      echo "[ERROR] Failed installation due to missing GNU wget. Please install GNU wget or curl and relaunch the script"
      exit 2
    fi
    version="`wget --quiet -qO- --header="Accept: application/vnd.github.v3+json" https://"$_github_api_url"/repos/thinger-io/"$_repo"/releases/latest | grep "tag_name" | cut -d '"' -f4`"
  fi
fi

if [ "$INIT" == "procd" ]; then
  if [ -f "$service_dir"/"$_module" ]; then
    service "$_module" stop
    service "$_module" disable
  fi
  if type curl > /dev/null; then
    curl -s -L -H "Accept: application/vnd.github.VERSION.raw" "https://$_github_api_url/repos/thinger-io/$_repo/contents/install/procd/$_module?ref=$version" -o "$service_dir/$_module"
  else
    wget -q --header="Accept: application/vnd.github.VERSION.raw" https://"$_github_api_url"/repos/thinger-io/"$_repo"/contents/install/procd/"$_module"?ref="$version" -P "$service_dir" -O "$service_dir"/"$_module"
  fi
  chmod +x "$service_dir/$_module"
elif [ "$INIT" == "systemd" ]; then
  # Download service file -> Before downloading binary
  if [ -f "$service_dir"/"$_module".service ]; then
    systemctl $sys_user stop "$_module".service
    systemctl $sys_user disable "$_module".service
  fi
  wget -q --header="Accept: application/vnd.github.VERSION.raw" https://"$_github_api_url"/repos/thinger-io/"$_repo"/contents/install/systemd/"$_module".template?ref="$version" -P "$service_dir" -O "$service_dir"/"$_module".template
  cat "$service_dir"/"$_module".template | envsubst '$home_dir,$certs_dir_env,$bin_dir,$config_dir' > "$service_dir"/"$_module".service
  rm -f "$service_dir"/"$_module".template
fi

# Download bin
if type curl > /dev/null; then
  version_release_body=$(curl -s -H "Accept: application/vnd.github.v3+json" "https://$_github_api_url/repos/thinger-io/$_repo/releases/tags/$version")
else
  version_release_body=`wget --quiet -qO- --header="Accept: application/vnd.github.v3+json" https://"$_github_api_url"/repos/thinger-io/"$_repo"/releases/tags/"$version"`
fi
download_url=`echo "$version_release_body" | grep "url.*$_arch" | cut -d '"' -f4`

if type curl > /dev/null; then
  curl -s -L -H "Accept: application/octet-stream" "$download_url" -o "$bin_dir/$_module"
else
  wget -q --header="Accept: application/octec-stream" "$download_url" -O "$bin_dir/$_module"
fi
chmod +x "$bin_dir"/"$_module"

# Provision iotmp client config
config_filename=iotmp.cfg
if [ ! -f "$config_filename" ]; then

    echo "Provisioning IOTMP client..."

    # initialize credentials
    if [ -z "${seed+x}" ]; then
      echo "Using default seed"
      KEY_HEX='4eee5a11576b5c532f068f71ab177d07'
    else
      KEY_HEX=$(echo -n "$seed" | hexdump -ve '/1 "%02x"')
    fi

    MAC=`ip link show $(ip route show default | awk '/default/ {print $5}') | awk '/link\/ether/ {print $2}' | sed 's/://g' | tr '[:lower:]' '[:upper:]'`
    if [ -z "${MAC+x}" ]; then
        MAC=`ip link show $(ip -o link | grep ether | awk '{ print $2}' | sed 's/://g') | awk '/link\/ether/ {print $2}' | sed 's/://g' | tr '[:lower:]' '[:upper:]'`
    fi

    if [ -z "${device+x}" ]; then
      device="$prefix"_"$MAC"
    fi

    if ! type openssl > /dev/null; then
      DEVICE_KEY="$device"
    else
     DEVICE_KEY=`echo $device | openssl sha256 -hex -mac HMAC -macopt hexkey:$KEY_HEX | awk '{print $2}'`
    fi

    cat > "$config_dir/$config_filename" << EOF
username=$username
device=$device
password=$DEVICE_KEY
host=$server
transport=$transport
EOF

fi

# Start and enable service
if [ "$INIT" == "procd" ]; then
  service "$_module" reload
  service "$_module" enable
  service "$_module" start
elif [ "$INIT" == "systemd" ]; then
  systemctl $sys_user daemon-reload
  systemctl $sys_user enable "$_module".service
  systemctl $sys_user start "$_module".service
fi

exit 0

