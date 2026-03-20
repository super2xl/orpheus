import { useState, useCallback, useRef } from 'react';
import { orpheus } from '../api/client';

// ── Response types ──────────────────────────────────────────────────────────

export interface CS2InitResult {
  class_count: number;
  schema_count: number;
  status?: string;
}

export interface CS2Player {
  index: number;
  name: string;
  team: number;
  team_name?: string;
  health: number;
  armor: number;
  kills: number;
  deaths: number;
  assists?: number;
  money: number;
  agent?: string;
  steam_id?: string;
  steam_id64?: string;
  address?: string;
  controller_address?: string;
  pawn_address?: string;
  is_local?: boolean;
  alive?: boolean;
}

export interface CS2GameState {
  map?: string;
  game_phase?: string;
  round?: number;
  ct_score?: number;
  t_score?: number;
  tick?: number;
  time?: number;
  status?: string;
  [key: string]: unknown;
}

export interface CS2Field {
  name: string;
  type: string;
  offset?: number;
  value?: string | number | boolean | null;
  [key: string]: unknown;
}

export interface CS2InspectResult {
  address: string;
  class_name?: string;
  type_name?: string;
  fields: CS2Field[];
  [key: string]: unknown;
}

export interface CS2EntityResult {
  address?: string;
  handle?: number;
  index?: number;
  class_name?: string;
  [key: string]: unknown;
}

// ── Hook ────────────────────────────────────────────────────────────────────

export function useCS2() {
  const [initialized, setInitialized] = useState(false);
  const [initResult, setInitResult] = useState<CS2InitResult | null>(null);
  const [initLoading, setInitLoading] = useState(false);
  const [initError, setInitError] = useState<string | null>(null);

  const [players, setPlayers] = useState<CS2Player[]>([]);
  const [playersLoading, setPlayersLoading] = useState(false);
  const [playersError, setPlayersError] = useState<string | null>(null);

  const [gameState, setGameState] = useState<CS2GameState | null>(null);
  const [gameStateLoading, setGameStateLoading] = useState(false);
  const [gameStateError, setGameStateError] = useState<string | null>(null);

  const [inspectResult, setInspectResult] = useState<CS2InspectResult | null>(null);
  const [inspectLoading, setInspectLoading] = useState(false);
  const [inspectError, setInspectError] = useState<string | null>(null);

  const playerRefreshRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const gameStateRefreshRef = useRef<ReturnType<typeof setInterval> | null>(null);

  // ── Init ──────────────────────────────────────────────────────────────────

  const init = useCallback(async (pid: number) => {
    setInitLoading(true);
    setInitError(null);
    try {
      const res = await orpheus.request<CS2InitResult>('tools/cs2_init', { pid }, { timeout: 30000 });
      setInitResult(res);
      setInitialized(true);
      return res;
    } catch (err: any) {
      setInitError(err.message);
      return null;
    } finally {
      setInitLoading(false);
    }
  }, []);

  // ── Players ───────────────────────────────────────────────────────────────

  const fetchPlayers = useCallback(async (pid: number) => {
    setPlayersLoading(true);
    setPlayersError(null);
    try {
      const res = await orpheus.request<{ players: CS2Player[] } | CS2Player[]>(
        'tools/cs2_list_players',
        { pid },
        { timeout: 10000 },
      );
      const list = Array.isArray(res) ? res : (res as any).players ?? [];
      setPlayers(list);
      return list as CS2Player[];
    } catch (err: any) {
      setPlayersError(err.message);
      return [];
    } finally {
      setPlayersLoading(false);
    }
  }, []);

  const startPlayerPolling = useCallback((pid: number) => {
    if (playerRefreshRef.current) clearInterval(playerRefreshRef.current);
    fetchPlayers(pid);
    playerRefreshRef.current = setInterval(() => fetchPlayers(pid), 3000);
    return () => {
      if (playerRefreshRef.current) clearInterval(playerRefreshRef.current);
    };
  }, [fetchPlayers]);

  const stopPlayerPolling = useCallback(() => {
    if (playerRefreshRef.current) {
      clearInterval(playerRefreshRef.current);
      playerRefreshRef.current = null;
    }
  }, []);

  // ── Game State ────────────────────────────────────────────────────────────

  const fetchGameState = useCallback(async (pid: number) => {
    setGameStateLoading(true);
    setGameStateError(null);
    try {
      const res = await orpheus.request<CS2GameState>('tools/cs2_get_game_state', { pid }, { timeout: 10000 });
      setGameState(res);
      return res;
    } catch (err: any) {
      setGameStateError(err.message);
      return null;
    } finally {
      setGameStateLoading(false);
    }
  }, []);

  const startGameStatePolling = useCallback((pid: number) => {
    if (gameStateRefreshRef.current) clearInterval(gameStateRefreshRef.current);
    fetchGameState(pid);
    gameStateRefreshRef.current = setInterval(() => fetchGameState(pid), 5000);
    return () => {
      if (gameStateRefreshRef.current) clearInterval(gameStateRefreshRef.current);
    };
  }, [fetchGameState]);

  const stopGameStatePolling = useCallback(() => {
    if (gameStateRefreshRef.current) {
      clearInterval(gameStateRefreshRef.current);
      gameStateRefreshRef.current = null;
    }
  }, []);

  // ── Entity / Inspect ─────────────────────────────────────────────────────

  const inspect = useCallback(async (pid: number, address: string, thisType?: string) => {
    setInspectLoading(true);
    setInspectError(null);
    try {
      const body: Record<string, unknown> = { pid, address };
      if (thisType) body.this_type = thisType;
      const res = await orpheus.request<CS2InspectResult>('tools/cs2_inspect', body, { timeout: 15000 });
      setInspectResult(res);
      return res;
    } catch (err: any) {
      setInspectError(err.message);
      return null;
    } finally {
      setInspectLoading(false);
    }
  }, []);

  const readField = useCallback(async (pid: number, address: string, className: string, fieldName: string) => {
    try {
      const res = await orpheus.request<{ value: unknown }>(
        'tools/cs2_read_field',
        { pid, address, class_name: className, field_name: fieldName },
        { timeout: 10000 },
      );
      return res;
    } catch {
      return null;
    }
  }, []);

  const getLocalPlayer = useCallback(async (pid: number) => {
    try {
      const res = await orpheus.request<CS2Player>('tools/cs2_get_local_player', { pid }, { timeout: 10000 });
      return res;
    } catch {
      return null;
    }
  }, []);

  const getEntity = useCallback(async (pid: number, handle?: number, index?: number) => {
    try {
      const body: Record<string, unknown> = { pid };
      if (handle !== undefined) body.handle = handle;
      if (index !== undefined) body.index = index;
      const res = await orpheus.request<CS2EntityResult>('tools/cs2_get_entity', body, { timeout: 10000 });
      return res;
    } catch {
      return null;
    }
  }, []);

  return {
    // Init
    initialized,
    initResult,
    initLoading,
    initError,
    init,
    // Players
    players,
    playersLoading,
    playersError,
    fetchPlayers,
    startPlayerPolling,
    stopPlayerPolling,
    // Game state
    gameState,
    gameStateLoading,
    gameStateError,
    fetchGameState,
    startGameStatePolling,
    stopGameStatePolling,
    // Inspect
    inspectResult,
    inspectLoading,
    inspectError,
    inspect,
    readField,
    getLocalPlayer,
    getEntity,
  };
}
