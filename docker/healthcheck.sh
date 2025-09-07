#!/bin/bash
# Health check script for ODR-AudioEnc StreamDAB Enhanced

set -e

# Configuration
API_PORT="${API_PORT:-8007}"
HEALTH_TIMEOUT="${HEALTH_TIMEOUT:-10}"
HEALTH_URL="http://localhost:${API_PORT}/api/v1/health"

# Function to log messages
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] HEALTHCHECK: $1"
}

# Function to check API health
check_api_health() {
    local url="$1"
    local timeout="$2"

    log "Checking API health at $url"

    if command -v curl >/dev/null; then
        response=$(curl -s --max-time "$timeout" --fail "$url" 2>/dev/null || echo "")
        if [ -n "$response" ]; then
            # Check if response contains expected health indicators
            if echo "$response" | grep -q '"api_healthy".*true'; then
                log "API health check passed"
                return 0
            else
                log "API health check failed: unhealthy response"
                return 1
            fi
        else
            log "API health check failed: no response"
            return 1
        fi
    elif command -v wget >/dev/null; then
        response=$(wget -q --timeout="$timeout" -O - "$url" 2>/dev/null || echo "")
        if [ -n "$response" ]; then
            if echo "$response" | grep -q '"api_healthy".*true'; then
                log "API health check passed"
                return 0
            else
                log "API health check failed: unhealthy response"
                return 1
            fi
        else
            log "API health check failed: no response"
            return 1
        fi
    else
        log "Warning: Neither curl nor wget available for HTTP health check"
        return 1
    fi
}

# Function to check process health
check_process_health() {
    log "Checking odr-audioenc process"

    if pgrep -f "odr-audioenc" >/dev/null; then
        log "odr-audioenc process is running"
        return 0
    else
        log "odr-audioenc process is not running"
        return 1
    fi
}

# Function to check port availability
check_port_health() {
    local port="$1"

    log "Checking if port $port is listening"

    if netstat -tuln 2>/dev/null | grep -q ":${port}"; then
        log "Port $port is listening"
        return 0
    elif ss -tuln 2>/dev/null | grep -q ":${port}"; then
        log "Port $port is listening"
        return 0
    else
        log "Port $port is not listening"
        return 1
    fi
}

# Function to check system resources
check_system_resources() {
    log "Checking system resources"

    # Check memory usage
    if command -v free >/dev/null; then
        mem_usage=$(free | awk '/^Mem:/{printf "%.1f", $3/$2 * 100}')
        log "Memory usage: ${mem_usage}%"

        # Alert if memory usage is too high
        if [ "$(echo "$mem_usage > 90" | bc 2>/dev/null || echo 0)" = "1" ]; then
            log "Warning: High memory usage (${mem_usage}%)"
        fi
    fi

    # Check disk space
    if command -v df >/dev/null; then
        disk_usage=$(df /opt/streamdab 2>/dev/null | awk 'NR==2{print $5}' | sed 's/%//')
        if [ -n "$disk_usage" ]; then
            log "Disk usage: ${disk_usage}%"

            # Alert if disk usage is too high
            if [ "$disk_usage" -gt 90 ]; then
                log "Warning: High disk usage (${disk_usage}%)"
            fi
        fi
    fi

    # Always return success for resource checks (warnings only)
    return 0
}

# Function to check log files
check_log_files() {
    log "Checking log files"

    local log_file="/var/log/odr-audioenc.log"
    local audit_log="/var/log/odr-audioenc-audit.log"

    # Check main log file
    if [ -f "$log_file" ]; then
        # Check if log file has been written to recently (within last 5 minutes)
        if [ $(find "$log_file" -mmin -5 | wc -l) -gt 0 ]; then
            log "Main log file is being written to"
        else
            log "Warning: Main log file not updated recently"
        fi

        # Check for recent errors
        if tail -100 "$log_file" 2>/dev/null | grep -i "error\|critical\|fatal" >/dev/null; then
            log "Warning: Recent errors found in log file"
        fi
    else
        log "Warning: Main log file not found"
    fi

    # Check audit log file
    if [ -f "$audit_log" ]; then
        log "Audit log file exists"
    fi

    return 0
}

# Function to perform comprehensive health check
comprehensive_health_check() {
    local exit_code=0
    local checks_passed=0
    local total_checks=0

    log "Starting comprehensive health check"

    # Check 1: Process health
    ((total_checks++))
    if check_process_health; then
        ((checks_passed++))
    else
        exit_code=1
    fi

    # Check 2: Port health (if API is enabled)
    if [ "${API_ENABLED:-true}" = "true" ]; then
        ((total_checks++))
        if check_port_health "$API_PORT"; then
            ((checks_passed++))
        else
            exit_code=1
        fi

        # Check 3: API health
        ((total_checks++))
        if check_api_health "$HEALTH_URL" "$HEALTH_TIMEOUT"; then
            ((checks_passed++))
        else
            exit_code=1
        fi
    fi

    # Check 4: System resources (always runs, but doesn't affect health status)
    ((total_checks++))
    if check_system_resources; then
        ((checks_passed++))
    fi

    # Check 5: Log files (always runs, warnings only)
    ((total_checks++))
    if check_log_files; then
        ((checks_passed++))
    fi

    # Summary
    log "Health check completed: $checks_passed/$total_checks checks passed"

    if [ $exit_code -eq 0 ]; then
        log "Overall health status: HEALTHY"
    else
        log "Overall health status: UNHEALTHY"
    fi

    return $exit_code
}

# Main execution
case "${1:-comprehensive}" in
    api)
        log "Running API-only health check"
        check_api_health "$HEALTH_URL" "$HEALTH_TIMEOUT"
        ;;
    process)
        log "Running process-only health check"
        check_process_health
        ;;
    port)
        log "Running port-only health check"
        check_port_health "$API_PORT"
        ;;
    resources)
        log "Running resource-only health check"
        check_system_resources
        ;;
    comprehensive|*)
        comprehensive_health_check
        ;;
esac

exit $?
