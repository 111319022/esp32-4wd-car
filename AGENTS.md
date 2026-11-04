# AGENTS.md

本專案給 AI 代理（Codex、Cursor、Gemini CLI 等）的規則。開始工作前請先讀 [`README.md`](README.md) 與 `docs/` 裡的文件（硬體 `hardware.md`、韌體 `software.md`、決策 `decisions.md`、測試 `testing.md`）。

最重要的幾條：

- 回覆與文件用繁體中文；程式碼識別字用英文，註解用繁體中文。
- 腳位與參數的唯一來源是 `firmware/car_main/config.h`。
- 安全規則不可違反：5 V 只接 ESP32 的 VIN、HC-SR04 的 Echo 必須分壓、手把斷線一定停車、不可移除電池低電壓停車。
- 主程式不可長時間阻塞，計時動作用 `millis()` 寫成狀態機。
- 硬體或設計決策變動前先問使用者，確定後更新 `docs/decisions.md` 與相關文件。
