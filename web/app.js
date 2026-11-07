"use strict";

const elements = {};
for (const id of ["source", "runtime-input", "source-state", "examples", "analyze", "run-js", "status", "comparison", "interpreter-output", "interpreter-meta", "javascript-output", "javascript-meta", "generated-code", "download-js", "tree", "tree-container", "tree-empty", "node-details", "zoom-level"])
    elements[id] = document.getElementById(id);

let analysis = null;
let collapsed = new Set();
let selected = null;
let selectedNode = null;
let viewRoot = null;
let zoom = 1;
let treeSize = {width: 1, height: 1};
let worker = null;
let workerTimer = null;
let workerUrl = null;

function status(text, state = "") {
    elements.status.textContent = text;
    elements.status.className = state;
}

function stopWorker() {
    if (worker) worker.terminate();
    if (workerTimer) clearTimeout(workerTimer);
    if (workerUrl) URL.revokeObjectURL(workerUrl);
    worker = null;
    workerTimer = null;
    workerUrl = null;
}

function invalidate() {
    stopWorker();
    analysis = null;
    viewRoot = null;
    selectedNode = null;
    document.getElementById("focus-node").disabled = true;
    document.getElementById("tree-path").textContent = "Program";
    elements["source-state"].textContent = "Needs analysis";
    elements["run-js"].disabled = true;
    elements["download-js"].disabled = true;
    elements["interpreter-output"].textContent = "Analyze a program to see its output.";
    elements["javascript-output"].textContent = "Run the generated code to compare results.";
    elements["interpreter-meta"].textContent = "";
    elements["javascript-meta"].textContent = "";
    elements.comparison.textContent = "Not compared";
    elements.comparison.className = "";
    elements["generated-code"].textContent = "Generated JavaScript will appear here.";
    elements.tree.replaceChildren();
    elements.tree.style.width = "0px";
    elements.tree.style.height = "0px";
    elements["tree-empty"].hidden = false;
    elements["node-details"].textContent = "No node selected.";
    status("Source or input changed. Analyze the current program.");
}

function describeError(error) {
    const location = error.column ? `${error.line}:${error.column}` : `line ${error.line}`;
    return `${error.stage} error at ${location}\n${error.message}`;
}

function showExecution(result, outputId, metaId) {
    elements[outputId].textContent = result.output || "(no output)";
    elements[metaId].textContent = result.ok ? `Exit code ${result.exitCode} · ${result.steps} steps` : describeError(result.error);
}

async function analyze() {
    stopWorker();
    const source = elements.source.value;
    const input = elements["runtime-input"].value;
    elements.analyze.disabled = true;
    elements["run-js"].disabled = true;
    status("Analyzing source, checking types and executing the AST...");
    try {
        const response = await fetch("/api/analyze", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({source, input})});
        const result = await response.json();
        if (elements.source.value !== source || elements["runtime-input"].value !== input) return;
        invalidate();
        if (!result.ok) {
            status(describeError(result.error), "error");
            elements["source-state"].textContent = "Check failed";
            selectLine(result.error.line);
            return;
        }
        analysis = result;
        collapsed = new Set();
        selected = null;
        viewRoot = result.ast;
        collapseOverview(result.ast);
        renderTree();
        setZoom(Math.min(1, (elements["tree-container"].clientWidth - 8) / treeSize.width));
        elements["source-state"].textContent = "Analyzed";
        elements["generated-code"].textContent = result.javascript;
        elements["run-js"].disabled = false;
        elements["download-js"].disabled = false;
        showExecution(result.execution, "interpreter-output", "interpreter-meta");
        status(result.execution.ok ? "Syntax and semantic checks passed. AST executed. JavaScript generated." : "Syntax and semantic checks passed. " + describeError(result.execution.error), result.execution.ok ? "success" : "error");
    } catch (error) {
        status("Cannot analyze the program. " + error.message, "error");
    } finally {
        elements.analyze.disabled = false;
    }
}

function runJavaScript() {
    if (!analysis) return;
    stopWorker();
    elements["run-js"].disabled = true;
    elements["javascript-output"].textContent = "Running...";
    workerUrl = URL.createObjectURL(new Blob([analysis.javascript + "\npostMessage(runMiniCpp(" + JSON.stringify(elements["runtime-input"].value) + "));"], {type: "text/javascript"}));
    worker = new Worker(workerUrl);
    worker.onmessage = event => {
        const result = event.data;
        showExecution(result, "javascript-output", "javascript-meta");
        const original = analysis.execution;
        const errorMessage = error => error.message.replace(/ '.*'$/, "");
        const match = original.ok === result.ok && original.output === result.output && (result.ok ? original.exitCode === result.exitCode : original.error.stage === result.error.stage && errorMessage(original.error) === errorMessage(result.error));
        elements.comparison.textContent = match ? "Results match" : "Results differ";
        elements.comparison.className = match ? "match" : "mismatch";
        stopWorker();
        elements["run-js"].disabled = false;
    };
    worker.onerror = event => {
        elements["javascript-output"].textContent = "JavaScript failed";
        elements["javascript-meta"].textContent = event.message;
        stopWorker();
        elements["run-js"].disabled = false;
    };
    workerTimer = setTimeout(() => {
        stopWorker();
        elements["javascript-output"].textContent = "Execution time limit exceeded";
        elements["run-js"].disabled = false;
    }, 5000);
}

function selectLine(line) {
    const lines = elements.source.value.split("\n");
    const index = Math.max(0, Math.min(line - 1, lines.length - 1));
    let start = 0;
    for (let current = 0; current < index; ++current) start += lines[current].length + 1;
    elements.source.focus({preventScroll: true});
    elements.source.setSelectionRange(start, start + lines[index].length);
    elements.source.scrollTop = Math.max(0, index * 22.4 - elements.source.clientHeight / 2);
}

function collapseOverview(node, depth = 0) {
    if (depth >= 3 && node.children.length) collapsed.add(node.id);
    for (const child of node.children) collapseOverview(child, depth + 1);
}

function svgElement(name, attributes = {}, text = "") {
    const element = document.createElementNS("http://www.w3.org/2000/svg", name);
    for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
    if (text) element.textContent = text;
    return element;
}

function renderTree() {
    if (!analysis) return;
    elements.tree.replaceChildren();
    elements["tree-empty"].hidden = true;
    const nodes = [];
    const edges = [];
    let row = 0;
    let depthMax = 0;
    function layout(node, depth) {
        const visible = collapsed.has(node.id) ? [] : node.children;
        const item = {node, x: depth * 205 + 18, y: 0};
        nodes.push(item);
        depthMax = Math.max(depthMax, depth);
        if (!visible.length) item.y = row++ * 82 + 18;
        else {
            const children = visible.map(child => layout(child, depth + 1));
            item.y = (children[0].y + children[children.length - 1].y) / 2;
            for (const child of children) edges.push({parent: item, child});
        }
        return item;
    }
    layout(viewRoot, 0);
    document.getElementById("tree-path").textContent = viewRoot.kind + (viewRoot.text ? " · " + viewRoot.text : "");
    treeSize = {width: depthMax * 205 + 215, height: row * 82 + 20};
    elements.tree.setAttribute("viewBox", `0 0 ${treeSize.width} ${treeSize.height}`);
    for (const {parent, child} of edges) {
        const startX = parent.x + 172;
        const startY = parent.y + 30;
        const endY = child.y + 30;
        elements.tree.append(svgElement("path", {class: "tree-edge", d: `M${startX},${startY} C${startX + 16},${startY} ${child.x - 16},${endY} ${child.x},${endY}`}));
    }
    for (const item of nodes) {
        const node = item.node;
        const group = svgElement("g", {class: `tree-node${selected === node.id ? " selected" : ""}${collapsed.has(node.id) ? " collapsed" : ""}`, transform: `translate(${item.x},${item.y})`, tabindex: "0", role: "button", "aria-label": `${node.kind} ${node.text} at line ${node.line}`});
        group.append(svgElement("title", {}, `${node.kind}${node.text ? " " + node.text : ""}\nType ${node.type}\nLine ${node.line}, column ${node.column}\n${node.children.length} children`));
        group.append(svgElement("rect", {width: 172, height: 61, rx: 5}));
        group.append(svgElement("text", {x: 9, y: 17, class: "node-kind"}, node.kind));
        const label = node.text.replaceAll("\n", "\\n").replaceAll("\t", "\\t") + (node.array && node.size > 0 ? `[${node.size}]` : "");
        group.append(svgElement("text", {x: 9, y: 34, class: "node-text"}, label.length > 21 ? label.slice(0, 20) + "…" : label));
        const suffix = collapsed.has(node.id) ? ` · +${node.children.length}` : "";
        group.append(svgElement("text", {x: 9, y: 50, class: "node-location"}, `line ${node.line}${node.type !== "unknown" ? " · " + node.type : ""}${suffix}`));
        const activate = () => {
            selected = node.id;
            selectedNode = node;
            document.getElementById("focus-node").disabled = false;
            if (node.children.length) {
                if (collapsed.has(node.id)) collapsed.delete(node.id);
                else collapsed.add(node.id);
            }
            elements["node-details"].textContent = `${node.kind} · ${JSON.stringify(node.text)} · type ${node.type}${node.array && node.size > 0 ? " · size " + node.size : ""} · line ${node.line}, column ${node.column} · symbol ${node.symbol} · ${node.children.length} children`;
            selectLine(node.line);
            renderTree();
        };
        group.onclick = activate;
        group.onkeydown = event => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); activate(); } };
        elements.tree.append(group);
    }
    setZoom(zoom);
}

function setZoom(value) {
    zoom = Math.max(0.25, Math.min(1.75, value));
    elements.tree.style.width = treeSize.width * zoom + "px";
    elements.tree.style.height = treeSize.height * zoom + "px";
    elements["zoom-level"].textContent = Math.round(zoom * 100) + "%";
}

for (const name of ["ast", "code", "help"]) {
    document.getElementById("tab-" + name).onclick = () => {
        for (const other of ["ast", "code", "help"]) {
            document.getElementById("tab-" + other).setAttribute("aria-selected", String(name === other));
            document.getElementById(other + "-view").hidden = name !== other;
        }
    };
}

elements.source.oninput = invalidate;
elements["runtime-input"].oninput = invalidate;
elements.source.onkeydown = event => {
    if (event.key === "Tab") {
        event.preventDefault();
        elements.source.setRangeText("    ", elements.source.selectionStart, elements.source.selectionEnd, "end");
        invalidate();
    }
};
elements.analyze.onclick = analyze;
elements["run-js"].onclick = runJavaScript;
document.getElementById("overview").onclick = () => { if (analysis) { collapsed.clear(); collapseOverview(viewRoot); renderTree(); setZoom(Math.min(1, (elements["tree-container"].clientWidth - 8) / treeSize.width)); } };
document.getElementById("expand").onclick = () => { collapsed.clear(); renderTree(); };
document.getElementById("focus-node").onclick = () => {
    if (!selectedNode) return;
    viewRoot = selectedNode;
    collapsed.clear();
    collapseOverview(viewRoot);
    renderTree();
    setZoom(1);
    elements["tree-container"].scrollTo(0, 0);
};
document.getElementById("whole-tree").onclick = () => {
    if (!analysis) return;
    viewRoot = analysis.ast;
    collapsed.clear();
    collapseOverview(viewRoot);
    renderTree();
    setZoom(Math.min(1, (elements["tree-container"].clientWidth - 8) / treeSize.width));
    elements["tree-container"].scrollTo(0, 0);
};
document.getElementById("zoom-out").onclick = () => setZoom(zoom - 0.15);
document.getElementById("zoom-in").onclick = () => setZoom(zoom + 0.15);
document.getElementById("fit").onclick = () => setZoom((elements["tree-container"].clientWidth - 8) / treeSize.width);
elements["download-js"].onclick = () => {
    if (!analysis) return;
    const url = URL.createObjectURL(new Blob([analysis.javascript], {type: "text/javascript"}));
    const link = document.createElement("a");
    link.href = url;
    link.download = "program.js";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
};

async function loadExample() {
    invalidate();
    const response = await fetch("/examples/" + encodeURIComponent(elements.examples.value));
    if (!response.ok) throw new Error("Cannot load example");
    elements.source.value = await response.text();
    elements["runtime-input"].value = elements.examples.value === "08_array_input.cpp" ? "5 4 1 5 2 3" : "";
    status("Example loaded. Analyze the program.");
}

elements.examples.onchange = () => loadExample().catch(error => status(error.message, "error"));

async function initialize() {
    try {
        const response = await fetch("/api/examples");
        const names = await response.json();
        for (const name of names) {
            const option = document.createElement("option");
            option.value = name;
            option.textContent = name;
            elements.examples.append(option);
        }
        await loadExample();
        await analyze();
    } catch (error) {
        status("Cannot initialize MiniCpp. " + error.message, "error");
    }
}

initialize();
