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
          address,
        });

        const callers = (result.xrefs || []).filter(
          (x) => x.mnemonic.toLowerCase() === 'call',
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
      const body: Record<string, unknown> = { pid, address: targetAddress };
      if (moduleBase) body.module_base = moduleBase;
      if (moduleSize) body.module_size = moduleSize;

      const xrefResult = await orpheus.request<{ xrefs: XrefResult[] }>('tools/find_xrefs', body, { timeout: 60000 });

      if (cancelledRef.current) return;

      // Step 2: Filter to write-like instructions
      const writeXrefs = (xrefResult.xrefs || []).filter((x) => {
        const m = x.mnemonic.toLowerCase();
        return m.startsWith('mov') || m === 'stosb' || m === 'stosd' || m === 'stosq' ||
               m === 'xchg' || m === 'cmpxchg' || m.startsWith('add') || m.startsWith('sub') ||
               m.startsWith('and') || m.startsWith('or') || m.startsWith('xor') ||
               m === 'inc' || m === 'dec' || m === 'neg' || m === 'not';
      });

      // Build WriteInfo from xrefs
      const functionNames = new Map<string, string>();
      const writeInfos: WriteInfo[] = writeXrefs.map((x) => {
        const parts = x.instruction.split(/\s+/);
        const mnemonic = parts[0] || x.mnemonic;
        const operands = parts.slice(1).join(' ');
        const funcAddr = x.module_offset
          ? `${x.module_name}+${x.module_offset}`
          : x.address;
        const funcName = x.module_name
          ? `${x.module_name}+${x.module_offset || '0x0'}`
          : `sub_${x.address.replace(/^0x/i, '')}`;
        functionNames.set(x.address, funcName);
        return {
          instruction_address: x.address,
          mnemonic,
          operands,
          full_text: x.instruction,
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
