const editor = document.querySelector("#sqlEditor");
const runButton = document.querySelector("#runButton");
const runLabel = document.querySelector("#runLabel");
const resetDatabaseButton = document.querySelector("#resetDatabaseButton");
const resetDatabaseLabel = document.querySelector("#resetDatabaseLabel");
const results = document.querySelector("#results");
const errorBox = document.querySelector("#errorBox");
const resultStatus = document.querySelector("#resultStatus");
const durationLabel = document.querySelector("#durationLabel");
const runtimeBadge = document.querySelector("#runtimeBadge");
const runtimeText = document.querySelector("#runtimeText");
const lineGutter = document.querySelector("#lineGutter");
const editorHint = document.querySelector("#editorHint");
const sampleSelect = document.querySelector("#sampleSelect");
const loadSampleButton = document.querySelector("#loadSampleButton");
const localFileInput = document.querySelector("#localFileInput");
const selectedFile = document.querySelector("#selectedFile");
const resultFilters = document.querySelector("#resultFilters");

let sequence = 0;
let bundledFiles = new Map();
let activeFilter = "all";

function uniqueTable(prefix = "demo") {
  sequence += 1;
  return `${prefix}_${String(Date.now()).slice(-7)}_${sequence}`;
}

const examples = {
  workflow() {
    const table = uniqueTable("student");
    return `-- 完整链路：建表 → 插入 → 查询 → 删除 → 再查询\nCREATE TABLE ${table}(id INT, name VARCHAR, age INT);\nINSERT INTO ${table} VALUES(1, 'Alice', 20);\nINSERT INTO ${table} VALUES(2, 'Bob', 17);\nINSERT INTO ${table} VALUES(3, 'Carol', 22);\nSELECT id, name FROM ${table} WHERE age >= 18;\nDELETE FROM ${table} WHERE id = 1;\nSELECT * FROM ${table};`;
  },
  filter() {
    const table = uniqueTable("scores");
    return `-- TRUE AND 被化简，10 + 8 被折叠为 18\nCREATE TABLE ${table}(id INT, name VARCHAR, score INT);\nINSERT INTO ${table} VALUES(1, 'Lin', 19);\nINSERT INTO ${table} VALUES(2, 'Ming', 16);\nSELECT name, score FROM ${table}\nWHERE TRUE AND score >= 10 + 8;`;
  },
  error() {
    return "-- 错误会显示阶段、错误码和源码位置\nSELECT missing_column FROM missing_table;";
  },
};

function setExample(name) {
  document.querySelectorAll(".example-chip").forEach((button) => {
    button.classList.toggle("active", button.dataset.example === name);
  });
  editor.value = examples[name]();
  selectedFile.textContent = "快速示例";
  editorHint.textContent = "快速示例使用唯一表名，可以重复运行。";
  sampleSelect.value = "";
  loadSampleButton.disabled = true;
  updateGutter();
  editor.focus();
}

function updateGutter() {
  const count = Math.max(1, editor.value.split("\n").length);
  lineGutter.textContent = Array.from({ length: count }, (_, index) => index + 1).join("\n");
}

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function renderStatus(status) {
  if (!status) return;
  const parts = status.database.replaceAll("\\", "/").split("/");
  document.querySelector("#databaseName").textContent = parts.at(-1) || status.database;
  document.querySelector("#databaseName").title = status.database;
  document.querySelector("#policyValue").textContent = status.replacement_policy;
  document.querySelector("#capacityValue").textContent = `${status.buffer_capacity} 页`;
  document.querySelector("#readRequests").textContent = status.stats.read_requests ?? 0;
  document.querySelector("#cacheHits").textContent = status.stats.cache_hits ?? 0;
  document.querySelector("#evictions").textContent = status.stats.evictions ?? 0;
  document.querySelector("#dirtyWrites").textContent = status.stats.dirty_writes ?? 0;
  document.querySelector("#tableCount").textContent = status.tables.length;

  const list = document.querySelector("#tableList");
  list.replaceChildren();
  if (!status.tables.length) {
    list.append(element("div", "empty-mini", "暂无数据表，先运行完整流程。"));
  } else {
    status.tables.forEach((table) => {
      const button = element("button", "table-item");
      button.type = "button";
      const label = element("span");
      label.append(element("strong", "", table.name));
      label.append(element("small", "", table.columns.map((column) => `${column.name}:${column.type}`).join(" · ")));
      button.append(label, element("span", "page-count", `${table.pages} DATA PAGE${table.pages === 1 ? "" : "S"}`));
      button.addEventListener("click", () => {
        editor.value = `SELECT * FROM ${table.name};`;
        updateGutter();
        editor.focus();
      });
      list.append(button);
    });
  }
}

function tokenText(tokens) {
  return (tokens || []).map((token) => {
    if (token && typeof token === "object") return token.lexeme ?? token.text ?? token.type ?? JSON.stringify(token);
    return String(token);
  }).join("  ");
}

function compilerBlock(title, content, full = false) {
  const block = element("div", `compiler-block${full ? " full" : ""}`);
  block.append(element("b", "", title), element("pre", "", content || "—"));
  return block;
}

function renderStatement(statement, index) {
  const card = element("article", "statement-card");
  card.dataset.category = statement.category || "other";
  const head = element("div", "statement-head");
  const left = element("div");
  const categoryName = { query: "查询结果", mutation: "数据变更", schema: "结构操作" }[statement.category] || "其他";
  left.append(
    element("span", "statement-index", String(index + 1).padStart(2, "0")),
    element("span", "statement-kind", statement.kind),
    element("span", `category-label ${statement.category || "other"}`, categoryName),
  );
  head.append(left, element("span", "statement-message", statement.result.message || `${statement.result.affected_rows} 行受影响`));
  card.append(head);

  if (statement.result.columns.length) {
    const wrap = element("div", "data-table-wrap");
    const table = element("table", "data-table");
    const thead = document.createElement("thead");
    const headerRow = document.createElement("tr");
    statement.result.columns.forEach((column) => headerRow.append(element("th", "", column)));
    thead.append(headerRow);
    const tbody = document.createElement("tbody");
    statement.result.rows.forEach((row) => {
      const tr = document.createElement("tr");
      row.forEach((value) => tr.append(element("td", "", String(value))));
      tbody.append(tr);
    });
    table.append(thead, tbody);
    wrap.append(table);
    card.append(wrap);
  } else {
    card.append(element("div", "message-result", statement.result.message || "执行成功"));
  }

  const detail = element("details", "compiler-detail");
  detail.append(element("summary", "", "查看编译细节与执行计划"));
  const content = element("div", "compiler-content");
  content.append(
    compilerBlock("Tokens", tokenText(statement.compiler.tokens), true),
    compilerBlock("AST", statement.compiler.ast),
    compilerBlock("Bound AST", statement.compiler.bound_ast),
    compilerBlock("Plan Before", statement.compiler.plan_before),
    compilerBlock("Plan After", statement.compiler.plan_after),
  );
  detail.append(content);
  card.append(detail);
  return card;
}

function applyResultFilter(filter) {
  activeFilter = filter;
  document.querySelectorAll(".filter-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.filter === filter);
  });
  results.querySelectorAll(".statement-card").forEach((card) => {
    card.hidden = filter !== "all" && card.dataset.category !== filter;
  });
}

function updateResultFilters(statements) {
  const counts = { all: statements.length, query: 0, mutation: 0, schema: 0 };
  statements.forEach((statement) => {
    if (Object.hasOwn(counts, statement.category)) counts[statement.category] += 1;
  });
  resultFilters.hidden = statements.length === 0;
  document.querySelectorAll(".filter-button").forEach((button) => {
    const count = counts[button.dataset.filter] ?? 0;
    button.querySelector("b").textContent = count;
    button.disabled = button.dataset.filter !== "all" && count === 0;
  });
  applyResultFilter(counts[activeFilter] ? activeFilter : "all");
}

function renderError(error) {
  if (!error) {
    errorBox.hidden = true;
    errorBox.replaceChildren();
    return;
  }
  errorBox.hidden = false;
  const position = error.line ? ` · 第 ${error.line} 行，第 ${error.column} 列` : "";
  errorBox.replaceChildren(
    element("strong", "", `${error.stage}:${error.code}${position}`),
    element("p", "", error.message),
  );
}

async function refreshStatus() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const payload = await response.json();
    renderStatus(payload);
    runtimeBadge.className = "runtime-badge online";
    runtimeText.textContent = `数据库在线 · v${payload.version}`;
  } catch (error) {
    runtimeBadge.className = "runtime-badge offline";
    runtimeText.textContent = "数据库连接失败";
  }
}

async function loadBundledExamples() {
  try {
    const response = await fetch("/api/examples", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const payload = await response.json();
    bundledFiles = new Map(payload.files.map((file) => [file.name, file]));
    payload.files.forEach((file) => {
      const option = document.createElement("option");
      option.value = file.name;
      option.textContent = `[${file.level}] ${file.title}`;
      sampleSelect.append(option);
    });
  } catch (error) {
    const option = document.createElement("option");
    option.textContent = "测试文件加载失败";
    option.disabled = true;
    sampleSelect.append(option);
  }
}

async function executeSql() {
  const sql = editor.value.trim();
  if (!sql) {
    renderError({ stage: "REQUEST", code: "EMPTY_SQL", message: "请输入至少一条 SQL 语句。" });
    return;
  }
  runButton.disabled = true;
  resetDatabaseButton.disabled = true;
  runLabel.textContent = "执行中…";
  resultStatus.className = "result-status idle";
  resultStatus.textContent = "RUNNING";
  renderError(null);
  try {
    const response = await fetch("/api/execute", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ sql }),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
    results.replaceChildren();
    payload.statements.forEach((statement, index) => results.append(renderStatement(statement, index)));
    if (!payload.statements.length) {
      results.append(element("div", "empty-mini", "没有可执行的 SQL 语句。"));
    }
    renderError(payload.error);
    updateResultFilters(payload.statements);
    renderStatus(payload.status);
    durationLabel.textContent = `${payload.statements.length} 条语句 · ${payload.duration_ms} ms`;
    resultStatus.className = `result-status ${payload.ok ? "success" : "error"}`;
    resultStatus.textContent = payload.ok ? "SUCCESS" : "ERROR";
  } catch (error) {
    renderError({ stage: "NETWORK", code: "REQUEST_FAILED", message: error.message });
    resultStatus.className = "result-status error";
    resultStatus.textContent = "ERROR";
  } finally {
    runButton.disabled = false;
    resetDatabaseButton.disabled = false;
    runLabel.textContent = "执行 SQL";
  }
}

async function resetDatabase() {
  resetDatabaseButton.disabled = true;
  runButton.disabled = true;
  resetDatabaseLabel.textContent = "清空中…";
  resultStatus.className = "result-status idle";
  resultStatus.textContent = "RESETTING";
  renderError(null);
  try {
    const response = await fetch("/api/database/reset", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ confirmation: "RESET_DATABASE" }),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);

    renderStatus(payload.status);
    resultFilters.hidden = true;
    activeFilter = "all";
    results.replaceChildren();
    const state = element("div", "empty-state");
    state.append(
      element("span", "empty-symbol", "✓"),
      element("h3", "", payload.removed_tables ? "演示数据库已清空" : "数据库已经是空的"),
      element(
        "p",
        "",
        payload.removed_tables
          ? `已移除 ${payload.removed_tables} 张表，当前 SQL 已保留，可以直接重新执行。`
          : "当前没有数据表，可以直接执行演示 SQL。",
      ),
    );
    if (payload.backup) {
      const backup = element("p", "reset-backup", `原数据库备份：${payload.backup}`);
      backup.title = payload.backup;
      state.append(backup);
    }
    results.append(state);
    editorHint.textContent = "数据库已清空，当前 SQL 已保留，可以直接重新执行。";
    durationLabel.textContent = `已清空 ${payload.removed_tables} 张表`;
    resultStatus.className = "result-status success";
    resultStatus.textContent = "RESET";
    editor.focus();
  } catch (error) {
    renderError({ stage: "RESET", code: "RESET_FAILED", message: error.message });
    resultStatus.className = "result-status error";
    resultStatus.textContent = "ERROR";
  } finally {
    resetDatabaseButton.disabled = false;
    runButton.disabled = false;
    resetDatabaseLabel.textContent = "清空数据库";
  }
}

document.querySelectorAll(".example-chip").forEach((button) => {
  button.addEventListener("click", () => setExample(button.dataset.example));
});
document.querySelector("#clearButton").addEventListener("click", () => {
  editor.value = "";
  updateGutter();
  editor.focus();
});
document.querySelector("#refreshButton").addEventListener("click", refreshStatus);
resetDatabaseButton.addEventListener("click", resetDatabase);
sampleSelect.addEventListener("change", () => {
  loadSampleButton.disabled = !bundledFiles.has(sampleSelect.value);
});
loadSampleButton.addEventListener("click", () => {
  const file = bundledFiles.get(sampleSelect.value);
  if (!file) return;
  editor.value = file.sql;
  selectedFile.textContent = file.name;
  selectedFile.title = file.name;
  editorHint.textContent = "重复演示前点击“清空数据库”，即可再次执行同一个测试文件。";
  document.querySelectorAll(".example-chip").forEach((button) => button.classList.remove("active"));
  updateGutter();
  editor.focus();
});
document.querySelector("#localFileButton").addEventListener("click", () => {
  localFileInput.value = "";
  localFileInput.click();
});
localFileInput.addEventListener("change", async () => {
  const file = localFileInput.files?.[0];
  if (!file) return;
  if (!file.name.toLowerCase().endsWith(".sql")) {
    renderError({ stage: "FILE", code: "INVALID_TYPE", message: "请选择扩展名为 .sql 的文本文件。" });
    return;
  }
  if (file.size > 1_000_000) {
    renderError({ stage: "FILE", code: "FILE_TOO_LARGE", message: "SQL 文件不能超过 1 MB。" });
    return;
  }
  try {
    editor.value = (await file.text()).replace(/^\uFEFF/, "");
    selectedFile.textContent = file.name;
    selectedFile.title = file.name;
    editorHint.textContent = "已载入本地 SQL 文件，可先预览内容再执行。";
    sampleSelect.value = "";
    loadSampleButton.disabled = true;
    document.querySelectorAll(".example-chip").forEach((button) => button.classList.remove("active"));
    renderError(null);
    updateGutter();
    editor.focus();
  } catch (error) {
    renderError({ stage: "FILE", code: "READ_FAILED", message: "无法读取所选 SQL 文件。" });
  }
});
document.querySelectorAll(".filter-button").forEach((button) => {
  button.addEventListener("click", () => applyResultFilter(button.dataset.filter));
});
runButton.addEventListener("click", executeSql);
editor.addEventListener("input", updateGutter);
editor.addEventListener("scroll", () => { lineGutter.scrollTop = editor.scrollTop; });
editor.addEventListener("keydown", (event) => {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    executeSql();
  }
  if (event.key === "Tab") {
    event.preventDefault();
    const start = editor.selectionStart;
    editor.setRangeText("  ", start, editor.selectionEnd, "end");
    updateGutter();
  }
});

setExample("workflow");
Promise.all([refreshStatus(), loadBundledExamples()]);
