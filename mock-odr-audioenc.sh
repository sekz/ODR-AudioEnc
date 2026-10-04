#!/bin/bash
# Mock ODR-AudioEnc - Simulates ODR-AudioEnc behavior for infrastructure testing
# Version: 1.0.0
# Component: AudioEnc Mock Binary

set -euo pipefail

# Configuration
LOG_DIR="/var/log/odr"
PID_FILE="/tmp/mock-audioenc-$$.pid"
START_TIME=$(date +%s)
FIFO_PATH=""
TCP_PORT=""
BITRATE="128"
CHANNELS="2"
SAMPLE_RATE="48000"
VERBOSITY="info"

# Logging function
log() {
    local level="$1"
    shift
    local message="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] [AudioEnc-Mock] $message" | tee -a "$LOG_DIR/audioenc-mock.log"
}

# Signal handlers
cleanup() {
    log "INFO" "Received shutdown signal, cleaning up..."

    # Remove PID file
    if [ -f "$PID_FILE" ]; then
        rm -f "$PID_FILE"
    fi

    # Close FIFO if exists
    if [ -n "$FIFO_PATH" ] && [ -p "$FIFO_PATH" ]; then
        log "INFO" "Closing FIFO: $FIFO_PATH"
    fi

    log "INFO" "Mock AudioEnc shutdown complete"
    exit 0
}

trap cleanup SIGTERM SIGINT SIGQUIT

# Parse command-line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -t|--tcp)
                TCP_PORT="$2"
                shift 2
                ;;
            -i|--input|--input=*)
                if [[ "$1" == --input=* ]]; then
                    # Handle --input=value format
                    FIFO_PATH="${1#*=}"
                    # Extract TCP port from tcp://*:PORT format
                    if [[ "$FIFO_PATH" =~ tcp://.*:([0-9]+) ]]; then
                        TCP_PORT="${BASH_REMATCH[1]}"
                        FIFO_PATH=""  # Will be set by --output
                    fi
                    shift
                else
                    FIFO_PATH="$2"
                    shift 2
                fi
                ;;
            -o|--output|--output=*)
                if [[ "$1" == --output=* ]]; then
                    FIFO_PATH="${1#*=}"
                    shift
                else
                    FIFO_PATH="$2"
                    shift 2
                fi
                ;;
            -b|--bitrate|--bitrate=*)
                if [[ "$1" == --bitrate=* ]]; then
                    BITRATE="${1#*=}"
                    shift
                else
                    BITRATE="$2"
                    shift 2
                fi
                ;;
            -c|--channels|--channels=*)
                if [[ "$1" == --channels=* ]]; then
                    CHANNELS="${1#*=}"
                    shift
                else
                    CHANNELS="$2"
                    shift 2
                fi
                ;;
            -r|--rate|--rate=*)
                if [[ "$1" == --rate=* ]]; then
                    SAMPLE_RATE="${1#*=}"
                    shift
                else
                    SAMPLE_RATE="$2"
                    shift 2
                fi
                ;;
            --format=*|--drift-comp|--enable-dab-rc)
                # Ignore ODR-specific flags that mock doesn't need
                shift
                ;;
            -v|--verbosity)
                VERBOSITY="$2"
                shift 2
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                log "WARN" "Unknown argument: $1"
                shift
                ;;
        esac
    done
}

show_help() {
    cat <<EOF
Mock ODR-AudioEnc - Simulates ODR-AudioEnc v3.6.0

Usage: odr-audioenc [OPTIONS]

Options:
  -t, --tcp PORT          TCP output port (required)
  -i, --input FIFO        Input FIFO path (required)
  -b, --bitrate RATE      Bitrate in kbps (default: 128)
  -c, --channels NUM      Number of channels (default: 2)
  -r, --rate HZ           Sample rate in Hz (default: 48000)
  -v, --verbosity LEVEL   Verbosity level (default: info)
  -h, --help              Show this help message

Example:
  odr-audioenc -t 8010 -i /tmp/fifo/audioenc.fifo -b 128 -c 2 -r 48000
EOF
}

# Initialize
init_encoder() {
    # Create log directory if needed
    mkdir -p "$LOG_DIR"

    # Write PID
    echo $$ > "$PID_FILE"

    log "INFO" "Starting Mock ODR-AudioEnc v3.6.0"
    log "INFO" "Configuration:"
    log "INFO" "  TCP Port: $TCP_PORT"
    log "INFO" "  Input FIFO: $FIFO_PATH"
    log "INFO" "  Bitrate: ${BITRATE}kbps"
    log "INFO" "  Channels: $CHANNELS"
    log "INFO" "  Sample Rate: ${SAMPLE_RATE}Hz"
    log "INFO" "  Verbosity: $VERBOSITY"

    # Create FIFO if it doesn't exist
    if [ ! -p "$FIFO_PATH" ]; then
        log "INFO" "Creating FIFO: $FIFO_PATH"
        FIFO_DIR=$(dirname "$FIFO_PATH")
        mkdir -p "$FIFO_DIR"
        mkfifo "$FIFO_PATH"
    fi

    # Simulate initialization delay
    sleep 2
    log "INFO" "AudioEnc initialized successfully"
}

# Main encoding loop
run_encoder() {
    local frame_count=0
    local last_log_time=$START_TIME

    log "INFO" "Encoder running, waiting for audio input on FIFO: $FIFO_PATH"
    log "INFO" "TCP output streaming on port: $TCP_PORT"

    # Main loop - simulates encoding
    while true; do
        sleep 5

        frame_count=$((frame_count + 1))
        local current_time=$(date +%s)
        local uptime=$((current_time - START_TIME))

        # Log status every 30 seconds
        if [ $((current_time - last_log_time)) -ge 30 ]; then
            log "INFO" "Encoding status: uptime=${uptime}s, frames=${frame_count}, bitrate=${BITRATE}kbps"
            last_log_time=$current_time
        fi

        # Verify FIFO still exists
        if [ ! -p "$FIFO_PATH" ]; then
            log "ERROR" "FIFO disappeared: $FIFO_PATH"
            exit 1
        fi
    done
}

# Main execution
main() {
    parse_args "$@"

    # Validate required arguments
    if [ -z "$TCP_PORT" ]; then
        log "ERROR" "TCP port is required (-t/--tcp)"
        show_help
        exit 1
    fi

    if [ -z "$FIFO_PATH" ]; then
        log "ERROR" "Input FIFO is required (-i/--input)"
        show_help
        exit 1
    fi

    init_encoder
    run_encoder
}

# Run main
main "$@"
