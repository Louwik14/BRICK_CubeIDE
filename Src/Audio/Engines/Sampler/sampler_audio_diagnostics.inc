void brick6_sampler_runtime_diag_reset(void)
{
    memset(&g_brick6_sampler_runtime_diag, 0, sizeof(g_brick6_sampler_runtime_diag));
    memset(g_sampler_multi_page0_reject_logged,
           0,
           sizeof(g_sampler_multi_page0_reject_logged));
}

void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return;
    }

    *out_snapshot = g_brick6_sampler_runtime_diag;
}

void brick6_sampler_runtime_get_health_snapshot(
    brick6_sampler_runtime_health_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return;
    }

    out_snapshot->multi_page_underrun =
        g_brick6_sampler_runtime_diag.multi_page_underrun;
    out_snapshot->multi_stop_underrun =
        g_brick6_sampler_runtime_diag.multi_stop_underrun;
    out_snapshot->multi_invalid_instrument_id =
        g_brick6_sampler_runtime_diag.multi_invalid_instrument_id;
}

