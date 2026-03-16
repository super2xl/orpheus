import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { ControlFlowGraph, ControlFlowGraphResponse, CFGNode } from '../api/types';

export function useCFG() {
  const [graph, setGraph] = useState<ControlFlowGraph | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const build = useCallback(async (pid: number, address: string) => {
    setLoading(true);
    setError(null);
    try {
      const result = await orpheus.request<ControlFlowGraphResponse>('tools/build_cfg', {
        pid,
        address,
      }, { timeout: 30000 });

      // Transform nodes array to Record<string, CFGNode> for panel usage
      const nodesRecord: Record<string, CFGNode> = {};
      for (const node of result.nodes) {
        nodesRecord[node.address] = node;
      }

      setGraph({
        nodes: nodesRecord,
        edges: result.edges,
        has_loops: result.has_loops,
        node_count: result.node_count,
        edge_count: result.edge_count,
      });
      setError(null);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  return { graph, loading, error, build };
}
