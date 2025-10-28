// ESP32 Touch Sensor Logger JavaScript

let activityChart;
let touchEvents = [];
let currentIndicators = [false, false, false, false, false, false, false];

// Initialize the application
async function init() {
    initChart();
    setupEventListeners();
    await loadTouchLogs();

    // Set up periodic updates
    setInterval(loadTouchLogs, 2000); // Update every 2 seconds
}

// Initialize Chart.js for activity overview
function initChart() {
    const ctx = document.getElementById('activityChart').getContext('2d');
    activityChart = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: ['Touch 1', 'Touch 2', 'Touch 3', 'Touch 4', 'Touch 5', 'Touch 6', 'Touch 7'],
            datasets: [{
                label: 'Touch Events',
                data: [0, 0, 0, 0, 0, 0, 0],
                backgroundColor: [
                    '#FF6384', '#36A2EB', '#FFCE56', '#4BC0C0',
                    '#9966FF', '#FF9F40', '#FF6384'
                ],
                borderWidth: 1
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                y: {
                    beginAtZero: true,
                    ticks: {
                        stepSize: 1
                    }
                }
            },
            plugins: {
                legend: {
                    display: false
                },
                title: {
                    display: true,
                    text: 'Touch Events by Sensor'
                }
            }
        }
    });
}

// Set up event listeners
function setupEventListeners() {
    document.getElementById('refresh-btn').addEventListener('click', loadTouchLogs);
    document.getElementById('export-btn').addEventListener('click', exportCSV);
}

// Load touch logs from API
async function loadTouchLogs() {
    try {
        const response = await fetch('/api/touch-logs');
        const logs = await response.json();

        updateLogsTable(logs);
        updateActivityChart(logs);
        updateStatusInfo(logs);

    } catch (error) {
        console.error('Error loading touch logs:', error);
        showNotification('Error loading logs. Please check connection.', 'error');
    }
}

// Update logs table
function updateLogsTable(logs) {
    const tbody = document.getElementById('logs-body');

    if (logs.length === 0) {
        tbody.innerHTML = '<tr><td colspan="3" class="no-data">No touch events recorded yet</td></tr>';
        return;
    }

    tbody.innerHTML = logs.map(log => `
        <tr>
            <td>${log.timestamp}</td>
            <td>${log.pad}</td>
            <td>${log.user}</td>
        </tr>
    `).join('');

    touchEvents = logs; // Store for export
}

// Update activity chart
function updateActivityChart(logs) {
    const touchCounts = [0, 0, 0, 0, 0, 0, 0];

    logs.forEach(log => {
        const padNumber = parseInt(log.pad.replace('Touch_', '')) - 1;
        if (padNumber >= 0 && padNumber < 7) {
            touchCounts[padNumber]++;
        }
    });

    activityChart.data.datasets[0].data = touchCounts;
    activityChart.update('none');
}

// Update status information
function updateStatusInfo(logs) {
    document.getElementById('total-events').textContent = logs.length;

    if (logs.length > 0) {
        const lastEvent = logs[logs.length - 1];
        const timestamp = new Date(lastEvent.timestamp);
        document.getElementById('last-event').textContent = timestamp.toLocaleTimeString();
    }

    // Simulate touch indicators (in real implementation, this would come from API)
    updateTouchIndicators();
}

// Simulate touch indicator updates
function updateTouchIndicators() {
    for (let i = 0; i < 7; i++) {
        const indicator = document.getElementById(`indicator-${i + 1}`);
        const wasActive = indicator.classList.contains('active');

        // Simulate random touch activity
        const isActive = Math.random() < 0.1; // 10% chance of being active

        if (isActive && !wasActive) {
            indicator.classList.add('active');
            setTimeout(() => indicator.classList.remove('active'), 500);
        }
    }
}

// Export logs as CSV
function exportCSV() {
    if (touchEvents.length === 0) {
        showNotification('No data to export', 'error');
        return;
    }

    // Create CSV content
    const csvHeader = 'Timestamp,Touch_Pad,User\n';
    const csvData = touchEvents.map(event =>
        `"${event.timestamp}","${event.pad}","${event.user}"`
    ).join('\n');

    const csvContent = csvHeader + csvData;

    // Create and download file
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const link = document.createElement('a');
    const url = URL.createObjectURL(blob);
    link.setAttribute('href', url);
    link.setAttribute('download', `touch_logs_${new Date().toISOString().split('T')[0]}.csv`);
    link.style.visibility = 'hidden';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);

    showNotification('CSV exported successfully!', 'success');
}

// Show notification messages
function showNotification(message, type) {
    // Simple notification - you could enhance this with a proper notification system
    const notification = document.createElement('div');
    notification.className = `notification ${type}`;
    notification.textContent = message;
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        padding: 15px 20px;
        border-radius: 5px;
        color: white;
        font-weight: bold;
        z-index: 1000;
        animation: slideIn 0.3s ease-out;
    `;

    if (type === 'success') {
        notification.style.backgroundColor = '#28a745';
    } else if (type === 'error') {
        notification.style.backgroundColor = '#dc3545';
    }

    document.body.appendChild(notification);

    // Add slide-in animation
    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideIn {
            from { transform: translateX(100%); opacity: 0; }
            to { transform: translateX(0); opacity: 1; }
        }
    `;
    document.head.appendChild(style);

    // Remove after 3 seconds
    setTimeout(() => {
        notification.style.animation = 'slideOut 0.3s ease-in';
        setTimeout(() => document.body.removeChild(notification), 300);
    }, 3000);
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', init);