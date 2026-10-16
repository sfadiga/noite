// Throwaway spike: capture window with a TipTap editor that reports readiness.
import { useEffect } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { EditorContent, useEditor } from "@tiptap/react";
import StarterKit from "@tiptap/starter-kit";

function App() {
  const editor = useEditor({ extensions: [StarterKit] });

  useEffect(() => {
    if (!editor) return;
    const unlisten = listen("capture:shown", () => {
      editor.commands.focus("end");
      requestAnimationFrame(() => requestAnimationFrame(() => invoke("report_ready")));
    });
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "Escape") return;
      e.preventDefault();
      const text = editor.getText();
      invoke("report_hidden", { text });
      editor.commands.clearContent();
    };
    window.addEventListener("keydown", onKey);
    return () => {
      unlisten.then((f) => f());
      window.removeEventListener("keydown", onKey);
    };
  }, [editor]);

  return (
    <main style={{ height: "100vh", padding: 16, boxSizing: "border-box" }}>
      <EditorContent editor={editor} style={{ height: "100%", outline: "none" }} />
    </main>
  );
}

export default App;
