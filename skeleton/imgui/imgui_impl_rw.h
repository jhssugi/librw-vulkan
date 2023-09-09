IMGUI_API bool ImGui_ImplRW_Init(void);
IMGUI_API void ImGui_ImplRW_Shutdown(void);
IMGUI_API void ImGui_ImplRW_NewFrame(float timeDelta);
#ifdef ENABLE_SKELETON
sk::EventStatus ImGuiEventHandler(sk::Event e, void *param);
#endif
void ImGui_ImplRW_RenderDrawLists(ImDrawData* draw_data);