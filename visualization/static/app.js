/**
 * PinnacleMM Strategy Performance Dashboard
 * Real-time visualization and monitoring for ML-enhanced market making
 */

class PinnacleDashboard {
    constructor() {
        this.ws = null;
        this.reconnectInterval = 5000;
        this.reconnectTimeout = null;
        this.maxReconnectAttempts = 10;
        this.reconnectAttempts = 0;
        this.isConnecting = false;
        this.charts = {};
        this.currentStrategy = '';
        this.performanceData = {};
        this.chartConfig = {
            timeframes: {
                '1h': 3600000,
                '4h': 14400000,
                '1d': 86400000,
                '1w': 604800000
            }
        };

        this.init();
    }

    init() {
        this.setupEventListeners();
        this.initializeCharts();
        this.connectWebSocket();
        this.updateServerTime();
        setInterval(() => this.updateServerTime(), 1000);
    }

    setupEventListeners() {
        // Strategy selector
        const strategySelect = document.getElementById('strategy-select');
        if (strategySelect) {
            strategySelect.addEventListener('change', (e) => {
                this.currentStrategy = e.target.value;
                this.subscribeToStrategy(this.currentStrategy);
            });
        }

        // Refresh strategies button
        const refreshBtn = document.getElementById('refresh-strategies');
        if (refreshBtn) {
            refreshBtn.addEventListener('click', () => this.requestStrategies());
        }

        // Tab switching
        document.querySelectorAll('.tab-button').forEach(button => {
            button.addEventListener('click', (e) => {
                this.switchTab(e.target.dataset.tab);
            });
        });

        // Timeframe selectors
        document.querySelectorAll('select[id$="-timeframe"]').forEach(select => {
            select.addEventListener('change', (e) => {
                this.updateChartTimeframe(e.target.id.replace('-timeframe', ''), e.target.value);
            });
        });

        // Connection modal handlers
        const connectBtn = document.getElementById('connect-btn');
        const cancelBtn = document.getElementById('cancel-btn');

        if (connectBtn) {
            connectBtn.addEventListener('click', () => this.handleManualConnect());
        }

        if (cancelBtn) {
            cancelBtn.addEventListener('click', () => this.hideConnectionModal());
        }

        // Backtest controls
        const runBacktestBtn = document.getElementById('run-backtest');
        const refreshBacktestsBtn = document.getElementById('refresh-backtests');

        if (runBacktestBtn) {
            runBacktestBtn.addEventListener('click', () => this.runBacktest());
        }

        if (refreshBacktestsBtn) {
            refreshBacktestsBtn.addEventListener('click', () => this.refreshBacktests());
        }
    }

    connectWebSocket() {
        if (this.isConnecting || (this.ws && this.ws.readyState === WebSocket.CONNECTING)) {
            return;
        }

        this.isConnecting = true;
        this.updateConnectionStatus('connecting', 'Connecting...');

        const wsHost = document.getElementById('ws-host')?.value || 'localhost:8080';
        const wsUrl = `ws://${wsHost}`;

        try {
            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                console.log('WebSocket connected');
                this.isConnecting = false;
                this.reconnectAttempts = 0;
                this.updateConnectionStatus('connected', 'Connected');
                this.requestStrategies();

                // Clear any pending reconnect
                if (this.reconnectTimeout) {
                    clearTimeout(this.reconnectTimeout);
                    this.reconnectTimeout = null;
                }
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.handleWebSocketMessage(data);
                } catch (error) {
                    console.error('Error parsing WebSocket message:', error);
                }
            };

            this.ws.onclose = () => {
                console.log('WebSocket disconnected');
                this.isConnecting = false;
                this.updateConnectionStatus('disconnected', 'Disconnected');
                this.scheduleReconnect();
            };

            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
                this.isConnecting = false;
                this.updateConnectionStatus('error', 'Connection Error');
            };

        } catch (error) {
            console.error('Error creating WebSocket:', error);
            this.isConnecting = false;
            this.updateConnectionStatus('error', 'Connection Failed');
            this.scheduleReconnect();
        }
    }

    scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            this.updateConnectionStatus('error', 'Max reconnect attempts reached');
            this.showConnectionModal();
            return;
        }

        if (this.reconnectTimeout) {
            clearTimeout(this.reconnectTimeout);
        }

        this.reconnectAttempts++;
        const delay = Math.min(this.reconnectInterval * this.reconnectAttempts, 30000);

        this.updateConnectionStatus('reconnecting', `Reconnecting in ${delay/1000}s... (${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

        this.reconnectTimeout = setTimeout(() => {
            this.connectWebSocket();
        }, delay);
    }

    updateConnectionStatus(status, message) {
        const statusElement = document.getElementById('connection-status');
        const statusDot = statusElement?.querySelector('.status-dot');
        const statusText = statusElement?.querySelector('span');

        if (statusDot) {
            statusDot.className = `status-dot ${status}`;
        }

        if (statusText) {
            statusText.textContent = message;
        }
    }

    handleWebSocketMessage(data) {
        console.log('Received WebSocket message:', data.type);

        switch (data.type) {
            case 'performance_update':
                this.handlePerformanceUpdate(data);
                break;
            case 'strategies_list':
                this.handleStrategiesList(data);
                break;
            case 'subscribe_ack':
                console.log('Subscription acknowledged:', data);
                break;
            case 'history_data':
                this.handleHistoryData(data);
                break;
            case 'error':
                console.error('WebSocket error:', data.message);
                break;
            default:
                console.log('Unknown message type:', data.type);
        }
    }

    handlePerformanceUpdate(data) {
        if (data.strategies) {
            for (const [strategyId, metrics] of Object.entries(data.strategies)) {
                this.performanceData[strategyId] = metrics;

                if (strategyId === this.currentStrategy || (!this.currentStrategy && strategyId === 'primary_strategy')) {
                    this.updateMetricsDisplay(metrics);
                    this.updateCharts(metrics);
                }
            }
        }
    }

    handleStrategiesList(data) {
        const select = document.getElementById('strategy-select');
        if (!select) return;

        // Clear existing options
        select.innerHTML = '<option value="">Select Strategy...</option>';

        if (data.strategies && Array.isArray(data.strategies)) {
            data.strategies.forEach(strategy => {
                const option = document.createElement('option');
                option.value = strategy.id;
                option.textContent = `${strategy.name} (${strategy.symbol})`;
                select.appendChild(option);
            });

            // Auto-select primary strategy if available
            if (data.strategies.length > 0 && !this.currentStrategy) {
                const primaryStrategy = data.strategies.find(s => s.id === 'primary_strategy') || data.strategies[0];
                select.value = primaryStrategy.id;
                this.currentStrategy = primaryStrategy.id;
                this.subscribeToStrategy(this.currentStrategy);
            }
        }
    }

    handleHistoryData(data) {
        if (data.strategy_id === this.currentStrategy) {
            this.updateChartWithHistory(data.metric, data.data);
        }
    }

    updateMetricsDisplay(metrics) {
        // Update metric cards
        this.updateElement('total-pnl', this.formatCurrency(metrics.pnl || 0));
        this.updateElement('sharpe-ratio', this.formatNumber(metrics.sharpe_ratio || 0, 2));
        this.updateElement('max-drawdown', this.formatPercentage(metrics.max_drawdown || 0));
        this.updateElement('win-rate', this.formatPercentage(metrics.win_rate || 0));
        this.updateElement('total-trades', metrics.total_trades || 0);
        this.updateElement('ml-accuracy', this.formatPercentage(metrics.ml_accuracy || 0));

        // Update ML status
        const mlStatus = document.getElementById('ml-model-status');
        if (mlStatus) {
            const statusDot = mlStatus.querySelector('.status-dot');
            const statusText = mlStatus.querySelector('span');

            if (metrics.ml_model_ready) {
                statusDot.className = 'status-dot connected';
                statusText.textContent = 'Model Ready';
            } else {
                statusDot.className = 'status-dot disconnected';
                statusText.textContent = 'Model Loading';
            }
        }

        this.updateElement('ml-predictions', metrics.ml_predictions || 0);
        this.updateElement('ml-pred-time', `${this.formatNumber(metrics.prediction_time || 0, 2)}μs`);

        // Update regime display
        this.updateRegimeDisplay(metrics.regime, metrics.regime_confidence);

        // Update regime metrics bars
        this.updateProgressBar('trend-strength', metrics.trend_strength || 0);
        this.updateProgressBar('volatility-level', metrics.volatility || 0);
        this.updateProgressBar('mean-reversion', metrics.mean_reversion || 0);
    }

    updateRegimeDisplay(regime, confidence) {
        const regimeNames = [
            'UNKNOWN', 'TRENDING_UP', 'TRENDING_DOWN', 'MEAN_REVERTING',
            'HIGH_VOLATILITY', 'LOW_VOLATILITY', 'CRISIS', 'CONSOLIDATION'
        ];

        const regimeName = regimeNames[regime] || 'UNKNOWN';
        this.updateElement('current-regime', regimeName);
        this.updateElement('regime-confidence', this.formatPercentage(confidence || 0));
    }

    updateProgressBar(id, value) {
        const bar = document.getElementById(id);
        if (bar) {
            const percentage = Math.max(0, Math.min(100, value * 100));
            bar.style.width = `${percentage}%`;
        }
    }

    subscribeToStrategy(strategyId) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN && strategyId) {
            const message = {
                type: 'subscribe',
                strategy_id: strategyId,
                data_type: 'performance'
            };
            this.ws.send(JSON.stringify(message));
        }
    }

    requestStrategies() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            const message = {
                type: 'get_strategies'
            };
            this.ws.send(JSON.stringify(message));
        }
    }

    switchTab(tabName) {
        // Remove active class from all tabs and content
        document.querySelectorAll('.tab-button').forEach(btn => btn.classList.remove('active'));
        document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));

        // Add active class to selected tab and content
        const tabButton = document.querySelector(`[data-tab="${tabName}"]`);
        const tabContent = document.getElementById(`${tabName}-tab`);

        if (tabButton) tabButton.classList.add('active');
        if (tabContent) tabContent.classList.add('active');

        // Resize charts when tab becomes visible
        setTimeout(() => {
            Object.values(this.charts).forEach(chart => {
                if (chart && typeof chart.resize === 'function') {
                    chart.resize();
                }
            });
        }, 100);
    }

    initializeCharts() {
        this.initPnLChart();
        this.initPositionChart();
        this.initSharpeChart();
        this.initMLAccuracyChart();
        this.initPredictionLatencyChart();
        this.initRegimeHistoryChart();
        this.initDrawdownChart();
    }

    initPnLChart() {
        const ctx = document.getElementById('pnl-chart');
        if (!ctx) return;

        this.charts.pnl = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'P&L',
                    data: [],
                    borderColor: '#00d4aa',
                    backgroundColor: 'rgba(0, 212, 170, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: {
                        type: 'time',
                        time: {
                            displayFormats: {
                                minute: 'HH:mm',
                                hour: 'HH:mm'
                            }
                        }
                    },
                    y: {
                        beginAtZero: false
                    }
                },
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initPositionChart() {
        const ctx = document.getElementById('position-chart');
        if (!ctx) return;

        this.charts.position = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Position',
                    data: [],
                    borderColor: '#0ea5e9',
                    backgroundColor: 'rgba(14, 165, 233, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initSharpeChart() {
        const ctx = document.getElementById('sharpe-chart');
        if (!ctx) return;

        this.charts.sharpe = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Sharpe Ratio',
                    data: [],
                    borderColor: '#f59e0b',
                    backgroundColor: 'rgba(245, 158, 11, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initMLAccuracyChart() {
        const ctx = document.getElementById('ml-accuracy-chart');
        if (!ctx) return;

        this.charts.mlAccuracy = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'ML Accuracy',
                    data: [],
                    borderColor: '#8b5cf6',
                    backgroundColor: 'rgba(139, 92, 246, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    y: {
                        min: 0,
                        max: 100
                    }
                },
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initPredictionLatencyChart() {
        const ctx = document.getElementById('prediction-latency-chart');
        if (!ctx) return;

        this.charts.predictionLatency = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Prediction Latency (μs)',
                    data: [],
                    borderColor: '#ef4444',
                    backgroundColor: 'rgba(239, 68, 68, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initRegimeHistoryChart() {
        const ctx = document.getElementById('regime-history-chart');
        if (!ctx) return;

        this.charts.regimeHistory = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Market Regime',
                    data: [],
                    borderColor: '#06b6d4',
                    backgroundColor: 'rgba(6, 182, 212, 0.1)',
                    fill: false,
                    stepped: true
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    y: {
                        min: 0,
                        max: 7,
                        ticks: {
                            stepSize: 1,
                            callback: function(value) {
                                const regimes = ['UNKNOWN', 'TREND_UP', 'TREND_DOWN', 'MEAN_REV', 'HIGH_VOL', 'LOW_VOL', 'CRISIS', 'CONSOL'];
                                return regimes[value] || value;
                            }
                        }
                    }
                },
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initDrawdownChart() {
        const ctx = document.getElementById('drawdown-chart');
        if (!ctx) return;

        this.charts.drawdown = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Drawdown %',
                    data: [],
                    borderColor: '#dc2626',
                    backgroundColor: 'rgba(220, 38, 38, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    y: {
                        reverse: true,
                        min: 0
                    }
                },
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    updateCharts(metrics) {
        const timestamp = new Date().toISOString();

        // Update P&L chart
        this.addDataPoint(this.charts.pnl, timestamp, metrics.pnl || 0);

        // Update position chart
        this.addDataPoint(this.charts.position, timestamp, metrics.position || 0);

        // Update Sharpe chart
        this.addDataPoint(this.charts.sharpe, timestamp, metrics.sharpe_ratio || 0);

        // Update ML accuracy chart
        this.addDataPoint(this.charts.mlAccuracy, timestamp, (metrics.ml_accuracy || 0) * 100);

        // Update prediction latency chart
        this.addDataPoint(this.charts.predictionLatency, timestamp, metrics.prediction_time || 0);

        // Update regime history chart
        this.addDataPoint(this.charts.regimeHistory, timestamp, metrics.regime || 0);

        // Update drawdown chart
        this.addDataPoint(this.charts.drawdown, timestamp, (metrics.max_drawdown || 0) * 100);
    }

    addDataPoint(chart, timestamp, value) {
        if (!chart) return;

        chart.data.labels.push(timestamp);
        chart.data.datasets[0].data.push(value);

        // Keep only last 100 data points
        if (chart.data.labels.length > 100) {
            chart.data.labels.shift();
            chart.data.datasets[0].data.shift();
        }

        chart.update('none');
    }

    updateChartTimeframe(chartName, timeframe) {
        // Request historical data for the new timeframe
        if (this.ws && this.ws.readyState === WebSocket.OPEN && this.currentStrategy) {
            const message = {
                type: 'get_history',
                strategy_id: this.currentStrategy,
                metric: chartName,
                time_range: this.chartConfig.timeframes[timeframe]
            };
            this.ws.send(JSON.stringify(message));
        }
    }

    updateChartWithHistory(metric, data) {
        const chart = this.charts[metric];
        if (!chart || !data) return;

        chart.data.labels = data.map(point => new Date(point.timestamp / 1000000).toISOString());
        chart.data.datasets[0].data = data.map(point => point.value);
        chart.update();
    }

    updateServerTime() {
        const timeElement = document.getElementById('server-time');
        if (timeElement) {
            timeElement.textContent = new Date().toLocaleTimeString();
        }
    }

    showConnectionModal() {
        const modal = document.getElementById('connection-modal');
        if (modal) {
            modal.classList.remove('hidden');
        }
    }

    hideConnectionModal() {
        const modal = document.getElementById('connection-modal');
        if (modal) {
            modal.classList.add('hidden');
        }
    }

    handleManualConnect() {
        this.hideConnectionModal();
        this.reconnectAttempts = 0;
        this.connectWebSocket();
    }

    runBacktest() {
        // Placeholder for backtest functionality
        console.log('Running backtest...');
        // In a real implementation, this would send a request to start a backtest
    }

    refreshBacktests() {
        // Placeholder for refreshing backtest results
        console.log('Refreshing backtests...');
        // In a real implementation, this would fetch latest backtest results
    }

    // Utility methods
    updateElement(id, value) {
        const element = document.getElementById(id);
        if (element) {
            element.textContent = value;
        }
    }

    formatCurrency(value) {
        return new Intl.NumberFormat('en-US', {
            style: 'currency',
            currency: 'USD'
        }).format(value);
    }

    formatNumber(value, decimals = 0) {
        return Number(value).toFixed(decimals);
    }

    formatPercentage(value) {
        return `${(value * 100).toFixed(2)}%`;
    }

    showLoading() {
        const overlay = document.getElementById('loading-overlay');
        if (overlay) {
            overlay.classList.remove('hidden');
        }
    }

    hideLoading() {
        const overlay = document.getElementById('loading-overlay');
        if (overlay) {
            overlay.classList.add('hidden');
        }
    }
}

// Initialize dashboard when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    window.dashboard = new PinnacleDashboard();
});

// Handle page visibility changes to pause/resume updates
document.addEventListener('visibilitychange', () => {
    if (window.dashboard) {
        if (document.hidden) {
            console.log('Page hidden, maintaining WebSocket connection');
        } else {
            console.log('Page visible, resuming full operation');
        }
    }
});

// Handle before unload to clean up WebSocket connection
window.addEventListener('beforeunload', () => {
    if (window.dashboard && window.dashboard.ws) {
        window.dashboard.ws.close();
    }
});
