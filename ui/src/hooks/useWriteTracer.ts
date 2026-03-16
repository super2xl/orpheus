import { useState, useCallback, useRef } from 'react';
import { orpheus } from '../api/client';
import type { WriteInfo, CallGraphNode, XrefResult } from '../api/types';

export function useWriteTracer() {
  const [writes, setWrites] = useState<WriteInfo[]>([]);
  const [callGraph, setCallGraph] = useState<CallGraphNode[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [progress, setProgress] = useState('');
  const cancelledRef = useRef(false);

  const buildCallGraph = useCallback(async (
    pid: number,
    functionAddresses: string[],
    functionNames: Map<string, string>,
    maxDepth: number,
  ): Promise<CallGraphNode[]> => {
    const visited = new Set<string>();
    const roots: CallGraphNode[] = [];

    async function buildNode(
      address: string,
      name: string,
      depth: number,
      type: string,
    ): Promise<CallGraphNode> {
      const node: CallGraphNode = { address, name, depth, type, children: [] };

      if (depth >= maxDepth || visited.has(address) || cancelledRef.current) {
        return node;
      }
      visited.add(address);

      try {
        const result = await orpheus.request<{ xrefs: XrefResult[] }>('tools/find_xrefs', {
          pid,
          target: address,
        });

        const callers = (result.xrefs || []).filter(
          (x) => x.type?.toLowerCase() === 'call',
        );

        for (const caller of callers) {
          if (cancelledRef.current) break;
          const callerName = functionNames.get(caller.address) || `sub_${caller.address.replace(/^0x/i, '')}`;
          const child = await buildNode(caller.address, callerName, depth + 1, 'Caller');
          node.children.push(child);
        }
      } catch {
        // Silently skip xref failures at depth
      }

      return node;
    }

    for (const addr of functionAddresses) {
      if (cancelledRef.current) break;
      const name = functionNames.get(addr) || `sub_${addr.replace(/^0x/i, '')}`;
      setProgress(`Building call graph for ${name}...`);
      const root = await buildNode(addr, name, 0, 'DirectWriter');
      roots.push(root);
    }

    return roots;
  }, []);

  const trace = useCallback(async (
    pid: number,
    targetAddress: string,
    moduleBase?: string,
    moduleSize?: number,
    maxDepth: number = 3,
  ) => {
    setLoading(true);
    setError(null);
    setWrites([]);
    setCallGraph([]);
    setProgress('Finding cross-references...');
    cancelledRef.current = false;

    try {
      // Step 1: Find all xrefs to target address
      const body: Record<string, unknown> = { pid, target: targetAddress };
      if (moduleBase) body.base = moduleBase;
      if (moduleSize) body.size = moduleSize;

      const xrefResult = await orpheus.request<{ xrefs: XrefResult[] }>('tools/find_xrefs', body, { timeout: 60000 });

      if (cancelledRef.current) return;

      // Step 2: Filter to write-like instructions based on xref type/context
      const writeXrefs = (xrefResult.xrefs || []).filter((x) => {
        const ctx = (x.context || '').toLowerCase();
        return ctx.startsWith('mov') || ctx.startsWith('stosb') || ctx.startsWith('stosd') || ctx.startsWith('stosq') ||
               ctx.startsWith('xchg') || ctx.startsWith('cmpxchg') || ctx.startsWith('add') || ctx.startsWith('sub') ||
               ctx.startsWith('and') || ctx.startsWith('or') || ctx.startsWith('xor') ||
               ctx.startsWith('inc') || ctx.startsWith('dec') || ctx.startsWith('neg') || ctx.startsWith('not');
      });

      // Build WriteInfo from xrefs
      const functionNames = new Map<string, string>();
      const writeInfos: WriteInfo[] = writeXrefs.map((x) => {
        const contextParts = (x.context || '').split(/\s+/);
        const mnemonic = contextParts[0] || x.type;
        const operands = contextParts.slice(1).join(' ');
        const funcAddr = x.address;
        const funcName = `sub_${x.address.replace(/^0x/i, '')}`;
        functionNames.set(x.address, funcName);
        return {
          instruction_address: x.address,
          mnemonic,
          operands,
          full_text: x.context || '',
          function_address: funcAddr,
          function_name: funcName,
        };
      });

      setWrites(writeInfos);
      setProgress(`Found ${writeInfos.length} writes. Building call graph...`);

      if (cancelledRef.current) return;

      // Step 3: Build call graph from unique function addresses
      const uniqueFunctions = [...new Set(writeInfos.map((w) => w.instruction_address))];
      if (uniqueFunctions.length > 0 && maxDepth > 0) {
        const graph = await buildCallGraph(pid, uniqueFunctions, functionNames, maxDepth);
        if (!cancelledRef.current) {
          setCallGraph(graph);
        }
      }

      setProgress('');
    } catch (err: any) {
      if (!cancelledRef.current) {
        setError(err.message);
      }
    } finally {
      if (!cancelledRef.current) {
        setLoading(false);
        setProgress('');
      }
    }
  }, [buildCallGraph]);

  const cancel = useCallback(() => {
    cancelledRef.current = true;
    setLoading(false);
    setProgress('');
  }, []);

  return { writes, callGraph, loading, error, progress, trace, cancel };
}
