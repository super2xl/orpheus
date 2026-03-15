import { useState, useCallback } from 'react';
import { orpheus } from '../api/client';
import type { EmulationResult } from '../api/types';

const REGISTER_NAMES = [
  'rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi', 'rbp', 'rsp',
  'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15',
  'rip', 'rflags',
];

function emptyRegisters(): Record<string, string> {
  const regs: Record<string, string> = {};
  for (const name of REGISTER_NAMES) {
    regs[name] = '0x0000000000000000';
  }
  return regs;
}

export function useEmulator() {
  const [created, setCreated] = useState(false);
  const [registers, setRegisters] = useState<Record<string, string>>(emptyRegisters());
  const [result, setResult] = useState<EmulationResult | null>(null);
  const [status, setStatus] = useState<string>('Not created');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [changedRegs, setChangedRegs] = useState<Set<string>>(new Set());

  const create = useCallback(async (pid: number) => {
    setLoading(true);
    setError(null);
    try {
      await orpheus.request<object>('emu_create', { pid });
      setCreated(true);
      setStatus('Ready');
      setRegisters(emptyRegisters());
      setResult(null);
      setChangedRegs(new Set());
    } catch (err: any) {
      setError(err.message);
      setStatus('Error');
    } finally {
      setLoading(false);
    }
  }, []);

  const destroy = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      await orpheus.request<object>('emu_destroy', {});
      setCreated(false);
      setStatus('Not created');
      setRegisters(emptyRegisters());
      setResult(null);
      setChangedRegs(new Set());
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const mapModule = useCallback(async (name: string) => {
    setLoading(true);
    setError(null);
    try {
      await orpheus.request<object>('emu_map_module', { module_name: name });
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const setRegister = useCallback(async (reg: string, value: string) => {
    setError(null);
    try {
      await orpheus.request<object>('emu_set_registers', {
        registers: { [reg]: value },
      });
      setRegisters((prev) => ({ ...prev, [reg]: value }));
    } catch (err: any) {
      setError(err.message);
    }
  }, []);

  const getRegisters = useCallback(async () => {
    setError(null);
    try {
      const res = await orpheus.request<{ registers: Record<string, string> }>('emu_get_registers', {});
      setRegisters(res.registers || emptyRegisters());
      return res.registers;
    } catch (err: any) {
      setError(err.message);
      return null;
    }
  }, []);

  const run = useCallback(async (start: string, end: string) => {
    setLoading(true);
    setError(null);
    setStatus('Running');
    setChangedRegs(new Set());
    const prevRegs = { ...registers };
    try {
      const res = await orpheus.request<EmulationResult>('emu_run', {
        start_address: start,
        end_address: end,
      }, { timeout: 30000 });
      setResult(res);
      setRegisters(res.registers || {});
      setStatus(res.success ? 'Completed' : 'Error');
      // Detect changed registers
      const changed = new Set<string>();
      for (const [key, val] of Object.entries(res.registers || {})) {
        if (prevRegs[key] !== val) changed.add(key);
      }
      setChangedRegs(changed);
    } catch (err: any) {
      setError(err.message);
      setStatus('Error');
    } finally {
      setLoading(false);
    }
  }, [registers]);

  const runInstructions = useCallback(async (start: string, count: number) => {
    setLoading(true);
    setError(null);
    setStatus('Running');
    setChangedRegs(new Set());
    const prevRegs = { ...registers };
    try {
      const res = await orpheus.request<EmulationResult>('emu_run_instructions', {
        start_address: start,
        count,
      }, { timeout: 30000 });
      setResult(res);
      setRegisters(res.registers || {});
      setStatus(res.success ? 'Completed' : 'Error');
      const changed = new Set<string>();
      for (const [key, val] of Object.entries(res.registers || {})) {
        if (prevRegs[key] !== val) changed.add(key);
      }
      setChangedRegs(changed);
    } catch (err: any) {
      setError(err.message);
      setStatus('Error');
    } finally {
      setLoading(false);
    }
  }, [registers]);

  const reset = useCallback(async () => {
    setError(null);
    try {
      await orpheus.request<object>('emu_reset', {});
      setRegisters(emptyRegisters());
      setResult(null);
      setStatus('Ready');
      setChangedRegs(new Set());
    } catch (err: any) {
      setError(err.message);
    }
  }, []);

  return {
    created,
    registers,
    result,
    status,
    loading,
    error,
    changedRegs,
    create,
    destroy,
    mapModule,
    setRegister,
    run,
    runInstructions,
    getRegisters,
    reset,
  };
}
