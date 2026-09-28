void brick6_sampler_runtime_diag_reset(void)
{
    memset(g_sampler_multi_stream_release_pending,
           0,
           sizeof(g_sampler_multi_stream_release_pending));
}
