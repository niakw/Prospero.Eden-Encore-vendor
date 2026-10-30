// Runs on the existing GPU worker, before the first guest Composite. Reuse its
// swapchain and composition pass without guest captures or FPS notifications.
void RendererVulkan::PresentLoading() {
    Frame* frame = present_manager.GetRenderFrame();
    scheduler.RequestOutsideRenderPassOperationContext();
    blit_swapchain.DrawToFrame(device, rasterizer, frame, {},
                              render_window.GetFramebufferLayout(), present_manager.SwapchainImageCount(),
                              present_manager.SwapchainImageViewFormat(), true);
    scheduler.Flush(*frame->render_ready);
    present_manager.Present(frame);
}
