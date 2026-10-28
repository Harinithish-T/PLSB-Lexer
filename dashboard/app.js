/**
 * PLSB Interactive Presentation Dashboard — Client Application
 * Modular 6-tab SPA: Analyzer, Buffer Visualization, Token Statistics, Markov Prediction, PLSB Comparison, About.
 * Retains renderReallocsChart and API hooks for complete endpoint and test compatibility.
 */

// Global State
const state = {
    currentSource: "",
    analyzedSource: "",
    analysisResult: null,
    tokens: [],
    analysisStale: false,
    markovAccuracy: null,
    markovAccuracyMethod: "",
    comparisonHistorical: null,
    comparisonRunning: false,
    bufferSimulation: {
        bufferSize: 40,
        buffer1: "",
        buffer2: "",
        activeBuffer: 1,
        charPointer: 0,
        forwardPointer: 0,
        currentChar: "",
        currentLexeme: "",
        refillCount: 0,
        spillCount: 0,
        isSpilled: false,
        position: 0,
        isPlaying: false,
        timer: null
    },
    markovTransitions: [],
    markovSelectedToken: "",
    freqMode: "type", // "type" | "lexeme"
    freqChart: null,
    cmpReallocChart: null,
    cmpCopyChart: null,
    legacyChart: null,
    isAnalyzing: false  // guard against duplicate concurrent analyze requests
};

/// Standard Example Programs matching Friend's Dashboard and PLSB
const EXAMPLES = {
    ex1: `#include <stdio.h>
int main() {
    int a = 10;
    int b = 20;
    int sum = a + b;
    printf("%d", sum);
    return 0;
}`,
    ex2: `int factorial(int n) {
    int result = 1;
    for (int i = 1; i <= n; i++) {
        result = result * i;
    }
    return result;
}`
};

// Keywords and Lexer mapping
const C_KEYWORDS = new Set([
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while"
]);

// Initialization
document.addEventListener("DOMContentLoaded", () => {
    initTabs();
    initAnalyzer();
    initBufferSimulation();
    initTokenStats();
    initMarkov();
    initComparison();

    const editor = document.getElementById("sourceCodeInput");
    if (editor) {
        editor.value = EXAMPLES.ex1;
        state.currentSource = EXAMPLES.ex1;
    }
    // Start in initial state (un-analyzed) as specified in Page 1 of PDF
    // clearAnalyzerDisplay resets tokens/results but we restore currentSource afterward
    clearAnalyzerDisplay("");
    // Restore Example 1 in editor after clearAnalyzerDisplay clears state.currentSource
    state.currentSource = EXAMPLES.ex1;
    if (editor) editor.value = EXAMPLES.ex1;
});

// =========================================================================
// TAB NAVIGATION
// =========================================================================
function initTabs() {
    const tabs = document.querySelectorAll(".nav-tab");
    tabs.forEach(tab => {
        tab.addEventListener("click", () => {
            tabs.forEach(t => t.classList.remove("active"));
            tab.classList.add("active");

            const targetTab = tab.dataset.tab;
            document.querySelectorAll(".tab-pane").forEach(pane => {
                pane.classList.remove("active");
            });

            const targetPane = document.getElementById(`pane-${targetTab}`);
            if (targetPane) targetPane.classList.add("active");

            // Chart resize adjustments
            if (targetTab === "statistics" && state.freqChart) {
                state.freqChart.resize();
            } else if (targetTab === "plsb") {
                if (state.cmpReallocChart) state.cmpReallocChart.resize();
                if (state.cmpCopyChart) state.cmpCopyChart.resize();
            }
        });
    });
}

// =========================================================================
// TAB 1: ANALYZER
// =========================================================================
function initAnalyzer() {
    const editor = document.getElementById("sourceCodeInput");
    const btnClear = document.getElementById("btnClearCode");
    const btnEx1 = document.getElementById("btnExample1");
    const btnEx2 = document.getElementById("btnExample2");
    const btnAnalyze = document.getElementById("btnAnalyzeCode");

    editor.addEventListener("input", () => {
        state.currentSource = editor.value;
        if (state.analyzedSource && editor.value !== state.analyzedSource) {
            invalidateStaleAnalysis("Source changed. Click Analyze Code to refresh tokens and statistics.");
        }
    });

    btnClear.addEventListener("click", () => {
        editor.value = "";
        state.currentSource = "";
        clearAnalyzerDisplay("");
    });

    btnEx1.addEventListener("click", () => {
        editor.value = EXAMPLES.ex1;
        state.currentSource = EXAMPLES.ex1;
        clearAnalyzerDisplay("");
    });

    btnEx2.addEventListener("click", () => {
        editor.value = EXAMPLES.ex2;
        state.currentSource = EXAMPLES.ex2;
        clearAnalyzerDisplay("");
    });

    btnAnalyze.addEventListener("click", () => {
        const code = editor.value;
        state.currentSource = code;
        analyzeSourceCode(code);
    });

    // Token search & filters
    const searchInput = document.getElementById("tokenSearchInput");
    const filterType = document.getElementById("tokenFilterType");
    if (searchInput) searchInput.addEventListener("input", renderTokenTable);
    if (filterType) filterType.addEventListener("change", renderTokenTable);

    // Export buttons
    const btnExportCsv = document.getElementById("btnExportTokensCsv");
    const btnExportJson = document.getElementById("btnExportTokensJson");
    const btnExportStats = document.getElementById("btnExportStatsCsv");
    if (btnExportCsv) btnExportCsv.addEventListener("click", exportTokensCsv);
    if (btnExportJson) btnExportJson.addEventListener("click", exportTokensJson);
    if (btnExportStats) btnExportStats.addEventListener("click", exportStatisticsCsv);
}

function setAnalysisMessage(text, kind) {
    const el = document.getElementById("analysisMessage");
    if (!el) return;
    if (!text) {
        el.style.display = "none";
        return;
    }
    el.textContent = text;
    el.className = "analysis-banner" + (kind ? ` ${kind}` : "");
    el.style.display = "block";
}

function invalidateStaleAnalysis(message) {
    state.analysisStale = true;
    state.tokens = [];
    state.analysisResult = null;
    state.analyzedSource = "";
    state.markovAccuracy = null;
    setAnalysisMessage(message, "warn");
    resetKpisUnavailable();
    const resultsContainer = document.getElementById("analyzerResultsContainer");
    const placeholderPanel = document.getElementById("analyzerPlaceholderPanel");
    if (resultsContainer) resultsContainer.style.display = "none";
    if (placeholderPanel) placeholderPanel.style.display = "block";
    resetMarkovEmpty("Source changed. Analyze again to rebuild transitions.");
    updateTokenFrequencies();
}

async function analyzeSourceCode(source) {
    if (!source || !source.trim()) {
        clearAnalyzerDisplay("");
        setAnalysisMessage("Please enter source code to analyze.", "error");
        return;
    }

    // Prevent duplicate concurrent requests
    if (state.isAnalyzing) return;
    state.isAnalyzing = true;

    const overlay = document.getElementById("analyzeLoadingOverlay");
    const statusBadge = document.getElementById("statusBadge");

    // Show loading overlay, hide any previous banner
    if (overlay) overlay.classList.add("active");
    setAnalysisMessage("", "");
    statusBadge.className = "badge badge-amber";
    statusBadge.textContent = "● Running Lexer...";

    try {
        const resp = await fetch("/api/run", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                source: source,
                buffer_size: 32,
                baseline_cap: 16,
                rule: "combined",
                mode: "compare"
            })
        });

        const data = await resp.json().catch(() => ({}));
        if (!resp.ok || data.success === false) {
            const err = data.error || `HTTP ${resp.status}`;
            throw new Error(err);
        }

        state.analysisResult = data;
        state.tokens = data.tokens_display || data.tokens_baseline || [];
        state.analyzedSource = source;
        state.currentSource = source;
        state.analysisStale = false;

        statusBadge.className = "badge badge-blue";
        statusBadge.textContent = "● Engine Ready";

        buildMarkovModelFromTokens();
        computeAndRenderStatistics(source, state.tokens);
        renderTokenTable();
        resetBufferSimulationWithSource(source);
        updateTokenFrequencies();

        // Switch to Analyzed State UI
        const resultsContainer = document.getElementById("analyzerResultsContainer");
        const placeholderPanel = document.getElementById("analyzerPlaceholderPanel");
        if (resultsContainer) resultsContainer.style.display = "block";
        if (placeholderPanel) placeholderPanel.style.display = "none";

        setAnalysisMessage("", "");
    } catch (err) {
        console.error("Analysis failed:", err);
        statusBadge.className = "badge badge-rose";
        statusBadge.textContent = "✗ Analysis Error";
        clearAnalyzerDisplay("");
        setAnalysisMessage("Analysis failed: " + err.message, "error");
    } finally {
        // Always hide overlay and release the guard
        if (overlay) overlay.classList.remove("active");
        state.isAnalyzing = false;
    }
}

function resetKpisUnavailable() {
    document.getElementById("kpiTotalTokens").textContent = "—";
    document.getElementById("kpiUniqueTokens").textContent = "—";
    document.getElementById("kpiKeywords").textContent = "—";
    const acc = document.getElementById("kpiAccuracy");
    const hint = document.getElementById("kpiAccuracyHint");
    if (acc) acc.textContent = "—";
    if (hint) hint.textContent = "Next-token type · unavailable";
    ["statChars","statLines","statTokens","statUnique","statKw","statId","statNum","statOp","statDelim","statCom","statErr"].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.textContent = "0";
    });
}

function clearAnalyzerDisplay(message) {
    state.tokens = [];
    state.analysisResult = null;
    state.analyzedSource = "";
    state.analysisStale = false;
    state.markovAccuracy = null;
    resetKpisUnavailable();
    const resultsContainer = document.getElementById("analyzerResultsContainer");
    const placeholderPanel = document.getElementById("analyzerPlaceholderPanel");
    if (resultsContainer) resultsContainer.style.display = "none";
    if (placeholderPanel) placeholderPanel.style.display = "block";

    const tbody = document.getElementById("tokenTableBody");
    if (tbody) {
        tbody.innerHTML = `<tr><td colspan="5" style="text-align: center; color: var(--text-dim); padding: 2rem;">No tokens analyzed yet.</td></tr>`;
    }
    const countEl = document.getElementById("tokenTableCount");
    if (countEl) countEl.textContent = "0";
    const filteredCountEl = document.getElementById("tokenTableFilteredCount");
    if (filteredCountEl) filteredCountEl.textContent = "0";

    resetBufferSimulationWithSource(state.currentSource || "");
    updateTokenFrequencies();
    resetMarkovEmpty("Analyze code to build the transition model.");
}

function computeAndRenderStatistics(source, tokens) {
    const totalChars = source.length;
    const totalLines = source ? source.split("\n").length : 0;
    const totalTokens = tokens.length;

    const uniqueSet = new Set(tokens.map(t => t.lexeme));
    let kwCount = 0;
    let idCount = 0;
    let numCount = 0;
    let opCount = 0;
    let delimCount = 0;
    let comCount = 0;
    let errCount = 0;

    tokens.forEach(t => {
        const type = t.type.toUpperCase();
        if (type.startsWith("KW_") || C_KEYWORDS.has(t.lexeme)) {
            kwCount++;
        } else if (type === "IDENTIFIER") {
            idCount++;
        } else if (type.includes("INT") || type.includes("FLOAT") || type.includes("NUM")) {
            numCount++;
        } else if (type.startsWith("OP_")) {
            opCount++;
        } else if (type === "COMMENT") {
            comCount++;
        } else if (type.includes("ERR") || type.includes("UNKNOWN")) {
            errCount++;
        } else {
            delimCount++;
        }
    });

    document.getElementById("kpiTotalTokens").textContent = totalTokens;
    document.getElementById("kpiUniqueTokens").textContent = uniqueSet.size;
    document.getElementById("kpiKeywords").textContent = kwCount;
    const acc = document.getElementById("kpiAccuracy");
    const hint = document.getElementById("kpiAccuracyHint");
    if (state.markovAccuracy == null) {
        if (acc) acc.textContent = "—";
        if (hint) hint.textContent = "Next-token type · unavailable";
    } else {
        if (acc) acc.textContent = `${state.markovAccuracy.toFixed(1)}%`;
        if (hint) hint.textContent = state.markovAccuracyMethod || "Next-token type";
    }

    document.getElementById("statChars").textContent = totalChars;
    document.getElementById("statLines").textContent = totalLines;
    document.getElementById("statTokens").textContent = totalTokens;
    document.getElementById("statUnique").textContent = uniqueSet.size;
    document.getElementById("statKw").textContent = kwCount;
    document.getElementById("statId").textContent = idCount;
    document.getElementById("statNum").textContent = numCount;
    document.getElementById("statOp").textContent = opCount;
    document.getElementById("statDelim").textContent = delimCount;
    document.getElementById("statCom").textContent = comCount;
    document.getElementById("statErr").textContent = errCount;
    document.getElementById("tokenTableCount").textContent = totalTokens;
}

function renderTokenTable() {
    const tbody = document.getElementById("tokenTableBody");
    const searchInput = document.getElementById("tokenSearchInput");
    const filterSelect = document.getElementById("tokenFilterType");
    const query = (searchInput ? searchInput.value : "").toLowerCase().trim();
    const filter = filterSelect ? filterSelect.value : "ALL";

    let filtered = state.tokens.filter(tok => {
        const matchQuery = !query || tok.lexeme.toLowerCase().includes(query) || tok.type.toLowerCase().includes(query);
        let matchFilter = true;
        if (filter !== "ALL") {
            const type = tok.type.toUpperCase();
            if (filter === "KEYWORD") matchFilter = type.startsWith("KW_") || C_KEYWORDS.has(tok.lexeme);
            else if (filter === "IDENTIFIER") matchFilter = type === "IDENTIFIER";
            else if (filter === "NUMBER") matchFilter = type.includes("INT") || type.includes("FLOAT");
            else if (filter === "STRING") matchFilter = type.includes("STR") || type.includes("CHAR");
            else if (filter === "OPERATOR") matchFilter = type.startsWith("OP_");
            else if (filter === "COMMENT") matchFilter = type === "COMMENT";
            else if (filter === "PREPROCESSOR") matchFilter = type === "PREPROCESSOR";
        }
        return matchQuery && matchFilter;
    });

    const filteredCountEl = document.getElementById("tokenTableFilteredCount");
    if (filteredCountEl) filteredCountEl.textContent = filtered.length;
    const totalCountEl = document.getElementById("tokenTableCount");
    if (totalCountEl) totalCountEl.textContent = state.tokens.length;

    if (filtered.length === 0) {
        tbody.innerHTML = `<tr><td colspan="5" style="text-align: center; color: var(--text-dim); padding: 1.5rem;">No matching tokens found.</td></tr>`;
        return;
    }

    let rowsHtml = "";
    filtered.slice(0, 500).forEach((tok, idx) => {
        let badgeClass = "tok-delim";
        const type = tok.type.toUpperCase();
        if (type.startsWith("KW_") || C_KEYWORDS.has(tok.lexeme)) badgeClass = "tok-kw";
        else if (type === "IDENTIFIER") badgeClass = "tok-id";
        else if (type.includes("INT") || type.includes("FLOAT")) badgeClass = "tok-num";
        else if (type.includes("STR") || type.includes("CHAR")) badgeClass = "tok-str";
        else if (type.startsWith("OP_")) badgeClass = "tok-op";
        else if (type === "COMMENT") badgeClass = "tok-com";
        else if (type === "PREPROCESSOR") badgeClass = "tok-pre";

        rowsHtml += `
            <tr>
                <td style="font-family: var(--font-mono); color: var(--text-dim); width: 50px;">${idx + 1}</td>
                <td style="font-family: var(--font-mono); font-weight: 600; color: white; max-width: 320px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${escapeHtml(tok.lexeme)}</td>
                <td><span class="tok-badge ${badgeClass}">${tok.type}</span></td>
                <td style="font-family: var(--font-mono); color: var(--text-muted); font-size: 0.82rem;">${tok.line}</td>
                <td style="font-family: var(--font-mono); color: var(--text-muted); font-size: 0.82rem;">${tok.col}</td>
            </tr>
        `;
    });

    tbody.innerHTML = rowsHtml;
}

function exportTokensCsv() {
    if (!state.tokens.length) return;
    const headers = ["ID", "Lexeme", "Token Type", "Line", "Column"];
    const rows = state.tokens.map((t, idx) => [
        idx + 1,
        `"${t.lexeme.replace(/"/g, '""')}"`,
        t.type,
        t.line,
        t.col
    ]);
    const csv = [headers.join(","), ...rows.map(r => r.join(","))].join("\n");
    downloadFile(csv, "token_table.csv", "text/csv");
}

function exportStatisticsCsv() {
    if (!state.tokens.length) return;
    const stats = [
        ["Total Characters", state.analyzedSource.length],
        ["Total Lines", state.analyzedSource.split("\n").length],
        ["Total Tokens", state.tokens.length],
        ["Unique Tokens", new Set(state.tokens.map(t => t.lexeme)).size],
        ["Accuracy", state.markovAccuracy != null ? `${state.markovAccuracy.toFixed(1)}%` : "N/A"]
    ];
    const csv = ["Metric,Value", ...stats.map(r => `"${r[0]}",${r[1]}`)].join("\n");
    downloadFile(csv, "program_statistics.csv", "text/csv");
}

function exportTokensJson() {
    if (!state.tokens.length) return;
    const report = {
        tokens: state.tokens,
        statistics: {
            totalCharacters: state.analyzedSource.length,
            totalLines: state.analyzedSource.split("\n").length,
            totalTokens: state.tokens.length,
            uniqueTokens: new Set(state.tokens.map(t => t.lexeme)).size,
            accuracy: state.markovAccuracy
        }
    };
    downloadFile(JSON.stringify(report, null, 2), "lexical_analysis_report.json", "application/json");
}

// =========================================================================
// TAB 2: BUFFER VISUALIZATION
// =========================================================================
function initBufferSimulation() {
    document.getElementById("btnBufStart").addEventListener("click", () => {
        // Initialize and IMMEDIATELY begin automatic traversal (no separate Play needed)
        resetBufferSimulationWithSource(state.currentSource);
        state.bufferSimulation.isPlaying = true;
        updatePlayPauseButton();
        runSimulationLoop();
    });

    document.getElementById("btnBufPlayPause").addEventListener("click", () => {
        state.bufferSimulation.isPlaying = !state.bufferSimulation.isPlaying;
        updatePlayPauseButton();
        if (state.bufferSimulation.isPlaying) {
            runSimulationLoop();
        } else {
            // Pause: cancel the active timer immediately
            if (state.bufferSimulation.timer) {
                clearTimeout(state.bufferSimulation.timer);
                state.bufferSimulation.timer = null;
            }
        }
    });

    document.getElementById("btnBufNext").addEventListener("click", () => {
        // Cancel automatic traversal, advance exactly one character
        state.bufferSimulation.isPlaying = false;
        if (state.bufferSimulation.timer) {
            clearTimeout(state.bufferSimulation.timer);
            state.bufferSimulation.timer = null;
        }
        updatePlayPauseButton();
        stepBufferSimulation();
    });

    document.getElementById("btnBufReset").addEventListener("click", () => {
        state.bufferSimulation.isPlaying = false;
        updatePlayPauseButton();
        resetBufferSimulationWithSource(state.currentSource);
    });
}

function updatePlayPauseButton() {
    const btn = document.getElementById("btnBufPlayPause");
    btn.innerHTML = state.bufferSimulation.isPlaying ? "⏸ Pause" : "▶ Play";
}

function resetBufferSimulationWithSource(source) {
    if (state.bufferSimulation.timer) {
        clearTimeout(state.bufferSimulation.timer);
        state.bufferSimulation.timer = null;
    }

    const B = state.bufferSimulation.bufferSize;
    const text = source || "";
    const b1 = text.slice(0, B).padEnd(B, "\0");
    const b2 = text.slice(B, B * 2).padEnd(B, "\0");

    state.bufferSimulation.buffer1 = b1;
    state.bufferSimulation.buffer2 = b2;
    state.bufferSimulation.activeBuffer = 1;
    state.bufferSimulation.charPointer = 0;
    state.bufferSimulation.forwardPointer = 0;
    state.bufferSimulation.currentChar = b1[0] !== "\0" ? b1[0] : "";
    state.bufferSimulation.currentLexeme = (b1[0] && b1[0] !== "\0" && !/\s/.test(b1[0])) ? b1[0] : "";
    state.bufferSimulation.refillCount = 0;
    state.bufferSimulation.spillCount = 0;
    state.bufferSimulation.isSpilled = false;
    state.bufferSimulation.position = 0;

    renderBufferBlocks();
    updateBufferMetrics();
}

function runSimulationLoop() {
    // Guard: stop if paused; clear any stale timer to prevent duplicate loops
    if (!state.bufferSimulation.isPlaying) return;
    if (state.bufferSimulation.timer) {
        clearTimeout(state.bufferSimulation.timer);
        state.bufferSimulation.timer = null;
    }
    stepBufferSimulation();

    const maxPos = state.currentSource ? state.currentSource.length : 0;
    if (state.bufferSimulation.position < maxPos && state.bufferSimulation.isPlaying) {
        // 250 ms per character — moderately faster than 400ms, still easy to follow
        state.bufferSimulation.timer = setTimeout(runSimulationLoop, 250);
    } else {
        state.bufferSimulation.isPlaying = false;
        state.bufferSimulation.timer = null;
        updatePlayPauseButton();
    }
}

function stepBufferSimulation() {
    const sim = state.bufferSimulation;
    const src = state.currentSource || "";
    if (sim.position >= src.length) return;

    sim.position += 1;
    sim.forwardPointer += 1;

    const B = sim.bufferSize;
    let justSpilled = false;

    if (sim.forwardPointer >= B) {
        sim.refillCount += 1;
        sim.forwardPointer = 0;
        sim.charPointer = 0;

        // Lexeme crossing buffer boundary triggers spill
        if (sim.currentLexeme.length > 0) {
            sim.isSpilled = true;
            sim.spillCount += 1;
            justSpilled = true;
        }

        if (sim.activeBuffer === 1) {
            const start = (sim.refillCount + 1) * B;
            sim.buffer1 = src.slice(start, start + B).padEnd(B, "\0");
            sim.activeBuffer = 2;
        } else {
            const start = (sim.refillCount + 1) * B;
            sim.buffer2 = src.slice(start, start + B).padEnd(B, "\0");
            sim.activeBuffer = 1;
        }
    } else {
        sim.charPointer = sim.forwardPointer;
    }

    const curBuf = sim.activeBuffer === 1 ? sim.buffer1 : sim.buffer2;
    const ch = curBuf[sim.charPointer] || "";
    sim.currentChar = ch === "\0" ? "" : ch;

    if (sim.currentChar && !/\s/.test(sim.currentChar) && !/[,;(){}[\]]/.test(sim.currentChar)) {
        sim.currentLexeme += sim.currentChar;
    } else if (sim.currentChar && /\s/.test(sim.currentChar)) {
        sim.currentLexeme = "";
        sim.isSpilled = false;
    } else if (sim.currentChar) {
        sim.currentLexeme = sim.currentChar;
        if (!justSpilled) sim.isSpilled = false;
    }

    renderBufferBlocks();
    updateBufferMetrics();
}

function renderBufferBlocks() {
    const sim = state.bufferSimulation;
    const b1Container = document.getElementById("bufCells1");
    const b2Container = document.getElementById("bufCells2");

    function renderCells(bufStr, isActive) {
        let html = "";
        for (let i = 0; i < bufStr.length; i++) {
            const ch = bufStr[i];
            const display = ch === "\0" ? "·" : (ch === " " ? "␣" : escapeHtml(ch));
            let cls = "buf-char";
            if (isActive && i === sim.charPointer) {
                cls += " char-ptr";
            } else if (isActive && i === sim.forwardPointer) {
                cls += " fwd-ptr";
            }
            html += `<span class="${cls}">${display}</span>`;
        }
        return html;
    }

    b1Container.innerHTML = renderCells(sim.buffer1, sim.activeBuffer === 1);
    b2Container.innerHTML = renderCells(sim.buffer2, sim.activeBuffer === 2);

    const b1Box = document.getElementById("bufBox1");
    const b2Box = document.getElementById("bufBox2");
    const b1Tag = document.getElementById("bufTag1");
    const b2Tag = document.getElementById("bufTag2");

    if (sim.activeBuffer === 1) {
        b1Box.className = "buffer-box active";
        b2Box.className = "buffer-box";
        b1Tag.style.display = "inline-flex";
        b2Tag.style.display = "none";
    } else {
        b1Box.className = "buffer-box";
        b2Box.className = "buffer-box active";
        b1Tag.style.display = "none";
        b2Tag.style.display = "inline-flex";
    }
}

function updateBufferMetrics() {
    const sim = state.bufferSimulation;
    const src = state.currentSource || "";

    document.getElementById("bufMetricActive").textContent = `Buffer ${sim.activeBuffer}`;
    document.getElementById("bufMetricCharPtr").textContent = sim.charPointer;
    document.getElementById("bufMetricFwdPtr").textContent = sim.forwardPointer;
    document.getElementById("bufMetricChar").textContent = sim.currentChar || "—";
    const lexEl = document.getElementById("bufMetricLexeme");
    const lex = sim.currentLexeme || "—";
    lexEl.textContent = lex;
    lexEl.title = sim.currentLexeme || "";
    document.getElementById("bufMetricRefills").textContent = sim.refillCount;
    document.getElementById("bufMetricSpills").textContent = sim.spillCount;
    document.getElementById("bufMetricSpillState").textContent = sim.isSpilled ? "ACTIVE (ACCUMULATING)" : "IDLE";
    document.getElementById("bufMetricSpillState").style.color = sim.isSpilled ? "#fbbf24" : "#94a3b8";

    const shown = src.length === 0 ? 0 : Math.min(sim.position + (sim.currentChar ? 1 : 0), src.length);
    const pct = src.length > 0 ? Math.round((Math.min(sim.position, src.length) / src.length) * 100) : 0;
    document.getElementById("bufProgressBar").style.width = `${pct}%`;
    document.getElementById("bufProgressText").textContent = `Character ${Math.min(sim.position, src.length)} of ${src.length} (${pct}%)`;
}

// =========================================================================
// TAB 3: TOKEN STATISTICS
// =========================================================================
function initTokenStats() {
    const btnType = document.getElementById("btnFreqModeType");
    const btnLexeme = document.getElementById("btnFreqModeLexeme");

    btnType.addEventListener("click", () => {
        state.freqMode = "type";
        btnType.className = "btn-primary";
        btnLexeme.className = "btn-secondary";
        updateTokenFrequencies();
    });

    btnLexeme.addEventListener("click", () => {
        state.freqMode = "lexeme";
        btnLexeme.className = "btn-primary";
        btnType.className = "btn-secondary";
        updateTokenFrequencies();
    });
}

function updateTokenFrequencies() {
    if (!state.tokens.length) {
        document.getElementById("freqMostFrequentVal").textContent = "—";
        document.getElementById("freqTableBody").innerHTML = `
            <tr><td colspan="4" style="text-align: center; color: var(--text-dim); padding: 2rem;">Analyze code to display frequency metrics.</td></tr>
        `;
        if (state.freqChart) {
            state.freqChart.destroy();
            state.freqChart = null;
        }
        return;
    }

    const counts = {};
    state.tokens.forEach(t => {
        const key = state.freqMode === "type" ? t.type : t.lexeme;
        counts[key] = (counts[key] || 0) + 1;
    });

    const total = state.tokens.length;
    const sorted = Object.entries(counts).map(([label, count]) => ({
        label,
        count,
        percentage: ((count / total) * 100).toFixed(1)
    })).sort((a, b) => b.count - a.count);

    document.getElementById("freqMostFrequentVal").textContent = `${sorted[0].label} (${sorted[0].count})`;

    // Render Table
    let tableHtml = "";
    sorted.forEach(item => {
        tableHtml += `
            <tr>
                <td style="font-family: var(--font-mono); font-weight: 600; color: white;">${escapeHtml(item.label)}</td>
                <td style="font-family: var(--font-mono);">${item.count}</td>
                <td style="font-family: var(--font-mono); color: var(--text-muted);">${item.percentage}%</td>
                <td style="width: 35%;">
                    <div style="height: 6px; border-radius: 9999px; background: rgba(51, 65, 85, 0.5); overflow: hidden;">
                        <div style="height: 100%; width: ${item.percentage}%; background: ${state.freqMode === "type" ? "#3b82f6" : "#a855f7"};"></div>
                    </div>
                </td>
            </tr>
        `;
    });
    document.getElementById("freqTableBody").innerHTML = tableHtml;

    // Render Chart.js
    const ctx = document.getElementById("freqChartCanvas").getContext("2d");
    const top12 = sorted.slice(0, 12);

    if (state.freqChart) {
        state.freqChart.destroy();
    }

    state.freqChart = new Chart(ctx, {
        type: "bar",
        data: {
            labels: top12.map(d => d.label),
            datasets: [{
                label: state.freqMode === "type" ? "Token Type Count" : "Lexeme Count",
                data: top12.map(d => d.count),
                backgroundColor: state.freqMode === "type" ? "rgba(59, 130, 246, 0.75)" : "rgba(168, 85, 247, 0.75)",
                borderColor: state.freqMode === "type" ? "#3b82f6" : "#a855f7",
                borderWidth: 1,
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { labels: { color: "#94a3b8" } },
                tooltip: {
                    callbacks: {
                        afterLabel: (ctx) => `Share: ${top12[ctx.dataIndex].percentage}%`
                    }
                }
            },
            scales: {
                x: {
                    grid: { color: "rgba(51, 65, 85, 0.4)" },
                    ticks: { color: "#94a3b8", font: { family: "'JetBrains Mono', monospace" } }
                },
                y: {
                    grid: { color: "rgba(51, 65, 85, 0.4)" },
                    ticks: { color: "#94a3b8" },
                    title: { display: true, text: "Count", color: "#94a3b8" }
                }
            }
        }
    });
}

// =========================================================================
// TAB 4: MARKOV PREDICTION (elements may not exist if tab was redesigned)
// =========================================================================
function initMarkov() {
    const select = document.getElementById("selectMarkovToken");
    if (!select) return; // Guard: element absent in PLSB Algorithm tab layout
    select.addEventListener("change", (e) => {
        state.markovSelectedToken = e.target.value;
        renderMarkovPredictions();
        renderMarkovTable();
        renderMarkovGraphForSelection();
    });
}

function resetMarkovEmpty(message) {
    state.markovTransitions = [];
    state.markovSelectedToken = "";
    state.markovAccuracy = null;
    state.markovAccuracyMethod = "";
    const select = document.getElementById("selectMarkovToken");
    if (select) select.innerHTML = "";
    const currentCard = document.getElementById("markovCurrentCard");
    if (currentCard) currentCard.textContent = "—";
    const predictedCard = document.getElementById("markovPredictedCard");
    if (predictedCard) predictedCard.textContent = "No prediction available";
    const probCard = document.getElementById("markovProbCard");
    if (probCard) probCard.textContent = "—";
    const topPills = document.getElementById("markovTopPills");
    if (topPills) topPills.innerHTML = `<span style="color: var(--text-dim); font-size: 0.8rem;">${escapeHtml(message || "Analyze code to build the Markov model.")}</span>`;
    const tbody = document.getElementById("markovTableBody");
    if (tbody) tbody.innerHTML = `
        <tr><td colspan="4" style="text-align: center; color: var(--text-dim); padding: 1.5rem;">${escapeHtml(message || "Select or analyze code to inspect Markov transitions.")}</td></tr>
    `;
    const svg = document.getElementById("markovSvgGraph");
    if (svg) svg.innerHTML = "";
}

function sortSuccessors(a, b) {
    if (b.count !== a.count) return b.count - a.count;
    return String(a.next).localeCompare(String(b.next));
}

function computeHeldOutTypeAccuracy(tokens) {
    // Full-sequence empirical accuracy: build model from all tokens, then evaluate
    // every consecutive transition t[i] -> t[i+1], matching the reference markov.ts approach.
    const types = tokens.filter(t => t.type && t.type !== "COMMENT").map(t => t.type);
    if (types.length < 3) {
        return { pct: null, method: "Next-token type · too few tokens" };
    }
    // Build full transition model
    const trans = {};
    for (let i = 0; i < types.length - 1; i++) {
        const cur = types[i];
        const next = types[i + 1];
        if (!trans[cur]) trans[cur] = {};
        trans[cur][next] = (trans[cur][next] || 0) + 1;
    }
    // Evaluate every transition in the sequence
    let correct = 0;
    let total = 0;
    for (let i = 0; i < types.length - 1; i++) {
        const cur = types[i];
        const actual = types[i + 1];
        const nextMap = trans[cur];
        if (!nextMap) continue;
        total += 1;
        const pred = Object.entries(nextMap)
            .map(([next, count]) => ({ next, count }))
            .sort(sortSuccessors)[0].next;
        if (pred === actual) correct += 1;
    }
    if (total === 0) {
        return { pct: null, method: "Next-token type · no evaluable transitions" };
    }
    return {
        pct: (100.0 * correct) / total,
        method: `Next-token type · ${correct}/${total} transitions`
    };
}

function buildMarkovModelFromTokens() {
    const tokens = state.tokens.filter(t => t.type !== "COMMENT");
    const held = computeHeldOutTypeAccuracy(state.tokens);
    state.markovAccuracy = held.pct;
    state.markovAccuracyMethod = held.method;

    if (tokens.length < 2) {
        resetMarkovEmpty("Insufficient tokens to construct Markov model.");
        return;
    }

    const transitions = {};
    for (let i = 0; i < tokens.length - 1; i++) {
        const cur = tokens[i].type;
        const next = tokens[i + 1].type;
        if (!transitions[cur]) transitions[cur] = {};
        transitions[cur][next] = (transitions[cur][next] || 0) + 1;
    }

    const transitionList = [];
    const uniqueTokens = Object.keys(transitions).sort();

    uniqueTokens.forEach(cur => {
        const nextMap = transitions[cur];
        const total = Object.values(nextMap).reduce((a, b) => a + b, 0);
        Object.entries(nextMap).forEach(([next, count]) => {
            transitionList.push({
                cur,
                next,
                count,
                prob: count / total
            });
        });
    });

    state.markovTransitions = transitionList;

    const select = document.getElementById("selectMarkovToken");
    if (select) {
        select.innerHTML = uniqueTokens.map(tok => `<option value="${tok}">${tok}</option>`).join("");
        if (uniqueTokens.length) {
            state.markovSelectedToken = uniqueTokens[0];
            select.value = uniqueTokens[0];
        }
    } else if (uniqueTokens.length) {
        // DOM absent (PLSB Algorithm tab) — just track selected token in state
        state.markovSelectedToken = uniqueTokens[0];
    }

    renderMarkovPredictions();
    renderMarkovTable();
    renderMarkovGraphForSelection();
}

function renderMarkovPredictions() {
    const cur = state.markovSelectedToken;
    const currentCard = document.getElementById("markovCurrentCard");
    if (!currentCard) return; // Markov elements not present in current tab layout
    currentCard.textContent = cur || "—";

    const matches = state.markovTransitions
        .filter(t => t.cur === cur)
        .sort(sortSuccessors);

    const predictedCard = document.getElementById("markovPredictedCard");
    const probCard = document.getElementById("markovProbCard");
    const topPills = document.getElementById("markovTopPills");

    if (matches.length > 0) {
        const top = matches[0];
        if (predictedCard) predictedCard.textContent = top.next;
        if (probCard) probCard.textContent = `${(top.prob * 100).toFixed(1)}% probability`;
        const pillsHtml = matches.slice(0, 3).map(m => `
            <div style="background: rgba(30, 41, 59, 0.7); border: 1px solid var(--border-subtle); border-radius: var(--radius-md); padding: 0.5rem 0.85rem; font-family: var(--font-mono); font-size: 0.8rem;">
                <span style="color: white; font-weight: 600;">${m.next}</span>
                <span style="color: #c084fc; margin-left: 0.4rem;">${(m.prob * 100).toFixed(1)}%</span>
            </div>
        `).join("");
        if (topPills) topPills.innerHTML = pillsHtml;
    } else {
        if (predictedCard) predictedCard.textContent = "No prediction available";
        if (probCard) probCard.textContent = "—";
        if (topPills) topPills.innerHTML = `<span style="color: var(--text-dim); font-size: 0.8rem;">No outgoing transitions for this token type.</span>`;
    }
}

function renderMarkovTable() {
    const tbody = document.getElementById("markovTableBody");
    if (!tbody) return; // Markov elements not present in current tab layout
    const cur = state.markovSelectedToken;
    const sorted = state.markovTransitions
        .filter(t => !cur || t.cur === cur)
        .sort(sortSuccessors);

    if (!sorted.length) {
        tbody.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--text-dim); padding: 1.5rem;">No prediction available</td></tr>`;
        return;
    }

    let html = "";
    sorted.slice(0, 100).forEach(t => {
        html += `
            <tr>
                <td style="font-family: var(--font-mono); color: #93c5fd; font-weight: 600;">${t.cur}</td>
                <td style="font-family: var(--font-mono); color: #c084fc; font-weight: 600;">${t.next}</td>
                <td style="font-family: var(--font-mono);">${t.count}</td>
                <td style="font-family: var(--font-mono); color: #34d399;">${(t.prob * 100).toFixed(1)}%</td>
            </tr>
        `;
    });
    tbody.innerHTML = html;
}

function renderMarkovGraphForSelection() {
    const cur = state.markovSelectedToken;
    const outgoing = state.markovTransitions.filter(t => t.cur === cur).map(t => t.next);
    const nodes = [cur, ...outgoing].filter(Boolean);
    const unique = [...new Set(nodes)].slice(0, 8);
    renderMarkovGraphSvg(unique);
}

function renderMarkovGraphSvg(nodes) {
    const svg = document.getElementById("markovSvgGraph");
    if (!svg) return; // Markov SVG not present in current tab layout
    if (!nodes.length) {
        svg.innerHTML = "";
        return;
    }

    const width = 560;
    const height = 320;
    const cx = width / 2;
    const cy = height / 2;
    const radius = 110;

    const angleStep = (2 * Math.PI) / nodes.length;
    const coords = {};

    let elementsSvg = "";

    // Nodes
    nodes.forEach((name, i) => {
        const angle = i * angleStep;
        const x = cx + radius * Math.cos(angle);
        const y = cy + radius * Math.sin(angle);
        coords[name] = { x, y };
    });

    // Edges
    state.markovTransitions.forEach(t => {
        if (coords[t.cur] && coords[t.next] && t.cur !== t.next) {
            const p1 = coords[t.cur];
            const p2 = coords[t.next];
            const strokeWidth = Math.max(1, Math.min(4, t.prob * 5));
            elementsSvg += `
                <line x1="${p1.x}" y1="${p1.y}" x2="${p2.x}" y2="${p2.y}" 
                      stroke="rgba(168, 85, 247, 0.4)" stroke-width="${strokeWidth}" />
            `;
        }
    });

    // Node circles & labels
    nodes.forEach(name => {
        const p = coords[name];
        elementsSvg += `
            <circle cx="${p.x}" cy="${p.y}" r="22" fill="#1e293b" stroke="#3b82f6" stroke-width="2" />
            <text x="${p.x}" y="${p.y + 4}" fill="#f8fafc" font-size="9" font-family="'JetBrains Mono', monospace" text-anchor="middle" font-weight="bold">${name.slice(0, 4)}</text>
        `;
    });

    svg.innerHTML = elementsSvg;
}

// =========================================================================
// TAB 5: PLSB COMPARISON
// =========================================================================
function initComparison() {
    document.getElementById("btnRunComparison").addEventListener("click", runComparison);
    document.getElementById("btnExportCmpCsv").addEventListener("click", exportComparisonCsv);
    loadComparisonDatasets();
}

const DISPLAY_COMPARE_IDS = ["baseline_16", "baseline_32", "baseline_64", "combined"];
let latestComparisonResults = null;

function setCmpStatus(text, kind) {
    const el = document.getElementById("cmpStatus");
    if (!el) return;
    el.textContent = text;
    el.className = "analysis-banner" + (kind ? ` ${kind}` : "");
}

function resetComparisonMetrics(message) {
    const tbody = document.getElementById("cmpTableBody");
    if (tbody) {
        tbody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--text-dim); padding: 2rem;">${escapeHtml(message || "No comparison has been run yet.")}</td></tr>`;
    }
    if (state.cmpReallocChart) {
        state.cmpReallocChart.destroy();
        state.cmpReallocChart = null;
    }
    if (state.cmpCopyChart) {
        state.cmpCopyChart.destroy();
        state.cmpCopyChart = null;
    }
}

async function loadComparisonDatasets() {
    const sel = document.getElementById("selectCmpDataset");
    if (!sel) return;

    // Friendly display labels for dataset IDs
    const FRIENDLY_NAMES = {
        "stress_long_tokens": "Stress Test — Long Tokens & Comments",
        "sample_c": "Sample C Program (Loops & Functions)",
        "long_identifiers": "Long Identifiers (Enterprise / Codegen)",
        "long_comments": "Documentation Comments (Multi-paragraph)",
        "long_strings": "Long String Literals (JSON / SQL)",
        "realworld_c_sample": "Real-World Systems C (Bytecode VM)"
    };

    try {
        const resp = await fetch("/api/datasets");
        const list = await resp.json();
        const existing = Array.isArray(list) ? list.filter(d => d.exists) : [];
        const currentOpt = `<option value="CURRENT">Current source in Analyzer</option>`;
        const opts = existing.map(d => {
            const label = FRIENDLY_NAMES[d.id] || d.name;
            return `<option value="${escapeHtml(d.path)}">${escapeHtml(label)}</option>`;
        });
        sel.innerHTML = currentOpt + opts.join("");
        const stress = existing.find(d => d.id === "stress_long_tokens");
        if (stress) sel.value = stress.path;
    } catch (err) {
        setCmpStatus("Could not load dataset registry: " + err.message, "error");
    }
}

async function runComparison() {
    if (state.comparisonRunning) return;
    const datasetChoice = document.getElementById("selectCmpDataset").value;
    const bufferSize = parseInt(document.getElementById("selectCmpBufferSize").value, 10);
    const btn = document.getElementById("btnRunComparison");

    if (!bufferSize || bufferSize <= 0) {
        setCmpStatus("Buffer size must be a positive number.", "error");
        return;
    }
    if (datasetChoice === "CURRENT" && !(state.currentSource || "").trim()) {
        setCmpStatus("Analyzer source is empty. Paste code or pick a registered dataset.", "error");
        return;
    }

    if (latestComparisonResults) {
        state.comparisonHistorical = latestComparisonResults;
    }
    latestComparisonResults = null;
    state.comparisonRunning = true;
    btn.disabled = true;
    btn.textContent = "Running native comparison…";
    resetComparisonMetrics("Run in progress. Previous numbers are not shown as the current result.");
    const histNote = state.comparisonHistorical && state.comparisonHistorical.run_id
        ? ` Previous run ${state.comparisonHistorical.run_id} is historical only.`
        : "";
    setCmpStatus("Executing combined PLSB and fixed 16/32/64-byte baselines…" + histNote, "warn");
    const meta = document.getElementById("cmpRunMeta");
    if (meta) meta.textContent = "";

    const payload = { buffer_size: bufferSize };
    if (datasetChoice === "CURRENT") {
        payload.source = state.currentSource;
    } else {
        payload.dataset = datasetChoice;
    }

    try {
        const resp = await fetch("/api/compare", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });
        const data = await resp.json().catch(() => ({}));
        if (!resp.ok || data.success === false) {
            const err = data.error || `HTTP ${resp.status}`;
            resetComparisonMetrics("Comparison failed. Previous results (if any) are historical, not this run.");
            setCmpStatus("Comparison failed: " + err, "error");
            if (meta) {
                meta.textContent = data.run_id ? `Failed run ${data.run_id}` : "";
            }
            return;
        }
        latestComparisonResults = data;
        renderComparisonResults(data);
    } catch (err) {
        resetComparisonMetrics("Comparison failed. Previous results (if any) are historical, not this run.");
        setCmpStatus("Comparison execution error: " + err.message, "error");
    } finally {
        state.comparisonRunning = false;
        btn.disabled = false;
        btn.textContent = "Run Comparison";
    }
}

function pickCompareRun(runs, id) {
    return (runs || []).find(r => r.config_id === id) || null;
}

function formatMetricValue(value, kind) {
    if (value === null || value === undefined || Number.isNaN(value)) return "—";
    if (kind === "bool") return value ? "Yes" : "No";
    if (kind === "pct") return `${Number(value).toFixed(1)}%`;
    if (kind === "time") return `${Number(value).toFixed(6)}`;
    if (kind === "rate") return `${Number(value).toFixed(2)}`;
    return String(value);
}

function formatImprovement(pct) {
    if (pct === null || pct === undefined) {
        return `<span style="color: var(--text-dim);">N/A</span>`;
    }
    const n = Number(pct);
    if (n > 0) return `<span style="color: #34d399; font-weight: 600;">${n.toFixed(2)}% better</span>`;
    if (n < 0) return `<span style="color: #f87171; font-weight: 600;">${n.toFixed(2)}% (regression)</span>`;
    return `<span style="color: var(--text-dim);">0.00%</span>`;
}

function renderComparisonResults(data) {
    const tbody = document.getElementById("cmpTableBody");
    const allRuns = data.runs || [];
    const runs = DISPLAY_COMPARE_IDS.map(id => pickCompareRun(allRuns, id)).filter(Boolean);

    if (runs.length === 0) {
        tbody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--text-dim); padding: 2rem;">No comparison runs returned.</td></tr>`;
        return;
    }

    const b16 = pickCompareRun(runs, "baseline_16");
    const b32 = pickCompareRun(runs, "baseline_32");
    const b64 = pickCompareRun(runs, "baseline_64");
    const plsb = pickCompareRun(runs, "combined");
    const cell = (run, key, kind) => formatMetricValue(run ? run[key] : null, kind);

    const metrics = [
        { key: "token_equivalence_pct", label: "Token equivalence", unit: "% of tokens", kind: "pct", better: "higher" },
        { key: "lexeme_spill_events", label: "Boundary spill events", unit: "count", kind: "int", better: "context" },
        { key: "spill_reallocations", label: "Spill reallocations", unit: "count", kind: "int", better: "lower", improve: "realloc_improvement_pct" },
        { key: "growth_copy_bytes", label: "Growth-copy bytes", unit: "bytes", kind: "int", better: "lower", improve: "copy_improvement_pct" },
        { key: "peak_spill_capacity", label: "Peak spill capacity", unit: "bytes", kind: "int", better: "context" },
        { key: "wall_time_seconds", label: "Execution time", unit: "seconds", kind: "time", better: "lower" },
        { key: "tokens_per_second", label: "Throughput", unit: "tokens/s", kind: "rate", better: "higher", improve: "throughput_improvement_pct" }
    ];

    let rowsHtml = "";
    metrics.forEach(m => {
        const improveCell = m.improve && plsb
            ? formatImprovement(plsb[m.improve])
            : `<span style="color: var(--text-dim);">—</span>`;
        rowsHtml += `
            <tr>
                <td style="font-weight: 600; color: white;">${m.label}<div class="kpi-hint">${m.better === "lower" ? "Lower is better" : m.better === "higher" ? "Higher is better" : "Descriptive"}</div></td>
                <td style="color: var(--text-muted);">${m.unit}</td>
                <td style="font-family: var(--font-mono);">${cell(b16, m.key, m.kind)}</td>
                <td style="font-family: var(--font-mono);">${cell(b32, m.key, m.kind)}</td>
                <td style="font-family: var(--font-mono);">${cell(b64, m.key, m.kind)}</td>
                <td style="font-family: var(--font-mono); font-weight: 700; color: white;">${cell(plsb, m.key, m.kind)}</td>
                <td>${improveCell}</td>
            </tr>
        `;
    });
    tbody.innerHTML = rowsHtml;

    const meta = document.getElementById("cmpRunMeta");
    if (meta) {
        meta.textContent = `Run ${data.run_id || "—"} · ${data.dataset || ""} · B=${data.buffer_size} · improvement vs ${data.baseline_reference || "Fixed Baseline 16B"}`;
    }
    let status = "Native comparison complete. Improvement uses Fixed Baseline 16B. N/A means that baseline was zero (no division).";
    if (data.zero_note) status += " " + data.zero_note;
    setCmpStatus(status, data.zero_note ? "warn" : "ok");
    renderComparisonCharts(runs);
}

function renderComparisonCharts(runs) {
    const labels = runs.map(r => r.display_label.replace("Baseline ", "Base ").replace("PLSB ", ""));
    const reallocs = runs.map(r => r.spill_reallocations);
    const copies = runs.map(r => r.growth_copy_bytes);

    // 1. Reallocations Chart
    const ctx1 = document.getElementById("cmpReallocChartCanvas").getContext("2d");
    if (state.cmpReallocChart) state.cmpReallocChart.destroy();
    state.cmpReallocChart = new Chart(ctx1, {
        type: "bar",
        data: {
            labels,
            datasets: [{
                label: "Spill Reallocations",
                data: reallocs,
                backgroundColor: runs.map(r => r.category === "baseline" ? "rgba(245, 158, 11, 0.75)" : "rgba(59, 130, 246, 0.75)"),
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { ticks: { color: "#94a3b8", font: { size: 10 } } },
                y: { ticks: { color: "#94a3b8" }, grid: { color: "rgba(51, 65, 85, 0.4)" } }
            }
        }
    });

    // 2. Growth Copy Bytes Chart
    const ctx2 = document.getElementById("cmpCopyChartCanvas").getContext("2d");
    if (state.cmpCopyChart) state.cmpCopyChart.destroy();
    state.cmpCopyChart = new Chart(ctx2, {
        type: "bar",
        data: {
            labels,
            datasets: [{
                label: "Copy Overhead (Bytes)",
                data: copies,
                backgroundColor: runs.map(r => r.category === "baseline" ? "rgba(245, 158, 11, 0.75)" : "rgba(168, 85, 247, 0.75)"),
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { ticks: { color: "#94a3b8", font: { size: 10 } } },
                y: { ticks: { color: "#94a3b8" }, grid: { color: "rgba(51, 65, 85, 0.4)" } }
            }
        }
    });
}

function exportComparisonCsv() {
    if (!latestComparisonResults || !latestComparisonResults.runs) return;
    const headers = [
        "Configuration", "Category", "Token Equivalence %", "Boundary Spills",
        "Reallocations", "Realloc Improvement % vs 16B", "Copy Bytes",
        "Copy Improvement % vs 16B", "Peak Capacity", "Wall Time s", "Throughput tok/s"
    ];
    const rows = latestComparisonResults.runs
        .filter(r => DISPLAY_COMPARE_IDS.includes(r.config_id))
        .map(r => [
            `"${r.display_label}"`,
            r.category,
            r.token_equivalence_pct ?? "",
            r.lexeme_spill_events,
            r.spill_reallocations,
            r.realloc_improvement_pct ?? "N/A",
            r.growth_copy_bytes,
            r.copy_improvement_pct ?? "N/A",
            r.peak_spill_capacity,
            r.wall_time_seconds,
            Number(r.tokens_per_second).toFixed(2)
        ]);
    const csv = [headers.join(","), ...rows.map(r => r.join(","))].join("\n");
    downloadFile(csv, `plsb_comparison_${latestComparisonResults.buffer_size}B.csv`, "text/csv");
}

// =========================================================================
// TEST SUITE PRESERVATION / BACKWARD COMPATIBILITY
// =========================================================================
/**
 * Preserves the renderReallocsChart symbol required by scripts/test_dashboard_endpoints.py
 */
function renderReallocsChart(ctx, results) {
    if (!ctx) return;
    const runs = Array.isArray(results) ? results : (results && results.runs) || [];
    const labels = runs.length ? runs.map(r => r.display_label || r.config_id || "run") : ["No data"];
    const values = runs.length ? runs.map(r => r.spill_reallocations || 0) : [0];
    return new Chart(ctx, {
        type: 'bar',
        data: { labels, datasets: [{ label: "Spill reallocations", data: values }] },
        options: { responsive: true, maintainAspectRatio: false }
    });
}

// Utility Helpers
function escapeHtml(str) {
    return (str || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function downloadFile(content, fileName, mimeType) {
    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = fileName;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}
