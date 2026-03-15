import { useState, useCallback, useMemo, useRef } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useCFG } from '../hooks/useCFG';
import { useConnection } from '../hooks/useConnection';
import type { CFGNode, CFGEdge, InstructionInfo } from '../api/types';

function getMnemonicColor(category: string): string {
  switch (category) {
    case 'Call': return 'var(--syn-call)';
    case 'Jump':
    case 'ConditionalJump': return 'var(--syn-jump)';
    case 'Return': return 'var(--syn-return)';
    case 'Compare': return 'var(--syn-compare)';
    case 'Nop': return 'var(--syn-nop)';
    default: return 'var(--syn-keyword)';
  }
}

function getEdgeColor(edge: CFGEdge): string {
  if (edge.is_back_edge) return 'var(--text-muted)';
  switch (edge.type) {
    case 'Branch': return 'var(--syn-jump)';
    case 'FallThrough': return 'var(--text-muted)';
    case 'Unconditional': return 'var(--text-secondary)';
    case 'Call': return 'var(--syn-call)';
    default: return 'var(--text-muted)';
  }
}

function getEdgeDash(edge: CFGEdge): string {
  if (edge.is_back_edge) return '4 3';
  return 'none';
}

// Build SVG path between two nodes
function buildEdgePath(
  fromNode: CFGNode,
  toNode: CFGNode,
  edge: CFGEdge,
): string {
  const fromX = fromNode.x + fromNode.width / 2;
  const fromY = fromNode.y + fromNode.height;
  const toX = toNode.x + toNode.width / 2;
  const toY = toNode.y;

  const dy = toY - fromY;

  // Straight down for fall-through when aligned
  if (edge.type === 'FallThrough' && Math.abs(fromX - toX) < 5) {
    return `M ${fromX} ${fromY} L ${toX} ${toY}`;
  }

  // Back edge — loop around the right side
  if (edge.is_back_edge) {
    const loopOffset = Math.max(fromNode.width, toNode.width) / 2 + 40;
    const rightX = Math.max(fromX, toX) + loopOffset;
    return `M ${fromX} ${fromY} C ${fromX} ${fromY + 30}, ${rightX} ${fromY + 30}, ${rightX} ${fromY} L ${rightX} ${toY} C ${rightX} ${toY - 30}, ${toX} ${toY - 30}, ${toX} ${toY}`;
  }

  // Curved path for branches
  const midY = fromY + dy / 2;
  return `M ${fromX} ${fromY} C ${fromX} ${midY}, ${toX} ${midY}, ${toX} ${toY}`;
}

const MAX_INSTRUCTIONS_PER_NODE = 5;

function NodeBlock({
  node,
  isSelected,
  isHovered,
  onSelect,
  onHover,
  onLeave,
}: {
  node: CFGNode;
  isSelected: boolean;
  isHovered: boolean;
  onSelect: () => void;
  onHover: () => void;
  onLeave: () => void;
}) {
  const displayInstructions = node.instructions.slice(0, MAX_INSTRUCTIONS_PER_NODE);
  const hasMore = node.instructions.length > MAX_INSTRUCTIONS_PER_NODE;

  return (
    <g
      transform={`translate(${node.x}, ${node.y})`}
      onClick={onSelect}
      onMouseEnter={onHover}
      onMouseLeave={onLeave}
      style={{ cursor: 'pointer' }}
    >
      {/* Background rect */}
      <rect
        width={node.width}
        height={node.height}
        rx={6}
        ry={6}
        fill="var(--surface)"
        stroke={isSelected ? 'var(--active-border)' : isHovered ? 'var(--text-muted)' : 'var(--border)'}
        strokeWidth={isSelected ? 1.5 : 1}
        style={{ transition: 'stroke 0.1s ease' }}
      />
      {/* Header: address */}
      <rect
        width={node.width}
        height={20}
        rx={6}
        ry={6}
        fill={isSelected ? 'var(--active)' : 'var(--hover)'}
      />
      {/* Bottom rect to square off header bottom corners */}
      <rect
        y={10}
        width={node.width}
        height={10}
        fill={isSelected ? 'var(--active)' : 'var(--hover)'}
      />
      {/* Header divider */}
      <line
        x1={0}
        y1={20}
        x2={node.width}
        y2={20}
        stroke="var(--border)"
        strokeWidth={0.5}
      />
      {/* Address text */}
      <text
        x={8}
        y={14}
        fill="var(--text-secondary)"
        fontSize={9}
        fontFamily="'JetBrains Mono', monospace"
        fontWeight={500}
      >
        {node.address.replace(/^0x/i, '').toUpperCase().padStart(16, '0')}
      </text>
      {/* Loop header indicator */}
      {node.is_loop_header && (
        <text
          x={node.width - 8}
          y={14}
          fill="var(--syn-jump)"
          fontSize={9}
          fontFamily="'JetBrains Mono', monospace"
          textAnchor="end"
        >
          {'\u21BA'}
        </text>
      )}
      {/* Instructions */}
      {displayInstructions.map((inst: InstructionInfo, i: number) => (
        <g key={i}>
          <text
            x={8}
            y={36 + i * 14}
            fill={getMnemonicColor(inst.category)}
            fontSize={9}
            fontFamily="'JetBrains Mono', monospace"
            fontWeight={400}
          >
            {inst.mnemonic}
          </text>
          <text
            x={60}
            y={36 + i * 14}
            fill="var(--text)"
            fontSize={9}
            fontFamily="'JetBrains Mono', monospace"
            fontWeight={400}
          >
            {inst.operands.length > 28 ? inst.operands.slice(0, 28) + '\u2026' : inst.operands}
          </text>
        </g>
      ))}
      {/* "... N more" indicator */}
      {hasMore && (
        <text
          x={8}
          y={36 + displayInstructions.length * 14}
          fill="var(--text-muted)"
          fontSize={8}
          fontFamily="'JetBrains Mono', monospace"
        >
          {`\u2026 ${node.instructions.length - MAX_INSTRUCTIONS_PER_NODE} more`}
        </text>
      )}
    </g>
  );
}

function CFGViewer() {
  const { connected, health } = useConnection();
  const pid = health?.pid;
  const { graph, loading, error, build } = useCFG();

  const [address, setAddress] = useState('');
  const [hasBuilt, setHasBuilt] = useState(false);
  const [selectedNode, setSelectedNode] = useState<string | null>(null);
  const [hoveredNode, setHoveredNode] = useState<string | null>(null);

  // Pan/zoom state
  const [viewBox, setViewBox] = useState({ x: 0, y: 0, width: 1200, height: 900 });
  const [dragging, setDragging] = useState(false);
  const dragStart = useRef({ x: 0, y: 0, vbX: 0, vbY: 0 });
  const svgRef = useRef<SVGSVGElement>(null);

  const handleGo = useCallback(async () => {
    const input = address.trim();
    if (!input || !pid) return;

    let addrStr: string;
    if (input.startsWith('0x') || input.startsWith('0X')) {
      addrStr = input;
    } else {
      addrStr = '0x' + input;
    }
    setHasBuilt(true);
    setSelectedNode(null);
    build(pid, addrStr);
  }, [address, pid, build]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleGo();
    }
  }, [handleGo]);

  // Fit graph to viewport when graph changes
  useMemo(() => {
    if (!graph) return;
    const nodes = Object.values(graph.nodes);
    if (nodes.length === 0) return;

    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const node of nodes) {
      minX = Math.min(minX, node.x);
      minY = Math.min(minY, node.y);
      maxX = Math.max(maxX, node.x + node.width);
      maxY = Math.max(maxY, node.y + node.height);
    }

    const padding = 60;
    setViewBox({
      x: minX - padding,
      y: minY - padding,
      width: (maxX - minX) + padding * 2,
      height: (maxY - minY) + padding * 2,
    });
  }, [graph]);

  // Pan handlers
  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return;
    // Only start pan on background
    if ((e.target as Element).tagName === 'svg' || (e.target as Element).classList.contains('cfg-bg')) {
      setDragging(true);
      dragStart.current = { x: e.clientX, y: e.clientY, vbX: viewBox.x, vbY: viewBox.y };
    }
  }, [viewBox]);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    if (!dragging || !svgRef.current) return;
    const svg = svgRef.current;
    const rect = svg.getBoundingClientRect();
    const scaleX = viewBox.width / rect.width;
    const scaleY = viewBox.height / rect.height;

    const dx = (e.clientX - dragStart.current.x) * scaleX;
    const dy = (e.clientY - dragStart.current.y) * scaleY;

    setViewBox(prev => ({
      ...prev,
      x: dragStart.current.vbX - dx,
      y: dragStart.current.vbY - dy,
    }));
  }, [dragging, viewBox.width, viewBox.height]);

  const handleMouseUp = useCallback(() => {
    setDragging(false);
  }, []);

  // Zoom handler
  const handleWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault();
    const svg = svgRef.current;
    if (!svg) return;

    const rect = svg.getBoundingClientRect();
    const mouseX = e.clientX - rect.left;
    const mouseY = e.clientY - rect.top;

    // Mouse position in SVG coordinates
    const svgX = viewBox.x + (mouseX / rect.width) * viewBox.width;
    const svgY = viewBox.y + (mouseY / rect.height) * viewBox.height;

    const factor = e.deltaY > 0 ? 1.1 : 0.9;
    const newWidth = viewBox.width * factor;
    const newHeight = viewBox.height * factor;

    // Keep the mouse position fixed
    setViewBox({
      x: svgX - (mouseX / rect.width) * newWidth,
      y: svgY - (mouseY / rect.height) * newHeight,
      width: newWidth,
      height: newHeight,
    });
  }, [viewBox]);

  // Highlight edges connected to selected/hovered node
  const highlightedEdges = useMemo(() => {
    const target = selectedNode || hoveredNode;
    if (!target || !graph) return new Set<number>();
    const indices = new Set<number>();
    graph.edges.forEach((edge, i) => {
      if (edge.from === target || edge.to === target) {
        indices.add(i);
      }
    });
    return indices;
  }, [selectedNode, hoveredNode, graph]);

  // Tooltip content for hovered node
  const tooltipNode = useMemo(() => {
    if (!hoveredNode || !graph) return null;
    return graph.nodes[hoveredNode] || null;
  }, [hoveredNode, graph]);

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <motion.div
        className="shrink-0 px-6 pt-6 pb-4 space-y-4"
        initial={{ opacity: 0, y: -8 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.15, ease: 'easeOut' }}
      >
        {/* Title row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h1 className="text-lg tracking-tight" style={{ color: 'var(--text)', fontWeight: 500 }}>
              CFG
            </h1>
            {graph && (
              <>
                {graph.function_name && (
                  <span
                    className="text-xs px-2 py-0.5 rounded-md font-mono"
                    style={{
                      color: 'var(--text-secondary)',
                      background: 'var(--active)',
                    }}
                  >
                    {graph.function_name}
                  </span>
                )}
                <span
                  className="text-xs px-2 py-0.5 rounded-md font-mono"
                  style={{
                    color: 'var(--text-secondary)',
                    background: 'var(--active)',
                  }}
                >
                  {Object.keys(graph.nodes).length} blocks
                </span>
                {graph.has_loops && (
                  <span
                    className="text-xs px-2 py-0.5 rounded-md font-mono"
                    style={{
                      color: 'var(--syn-jump)',
                      background: 'var(--active)',
                    }}
                  >
                    loops
                  </span>
                )}
              </>
            )}
          </div>
        </div>

        {/* Address bar */}
        <div className="flex items-center gap-2">
          <div className="relative flex-1">
            <span
              className="absolute left-3 top-1/2 -translate-y-1/2 text-xs font-mono pointer-events-none"
              style={{ color: 'var(--text-muted)' }}
            >
              {'0x'}
            </span>
            <input
              type="text"
              placeholder="Function address"
              value={address}
              onChange={(e) => setAddress(e.target.value)}
              onKeyDown={handleKeyDown}
              className="w-full h-9 pl-8 pr-3 rounded-lg text-sm font-mono outline-none"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
                color: 'var(--text)',
                transition: 'border-color 0.1s ease',
              }}
              onFocus={(e) => {
                e.currentTarget.style.borderColor = 'var(--text-muted)';
              }}
              onBlur={(e) => {
                e.currentTarget.style.borderColor = 'var(--border)';
              }}
            />
          </div>
          <button
            onClick={handleGo}
            disabled={loading || !pid || !address.trim()}
            className="px-4 h-9 rounded-lg text-sm cursor-pointer border-none outline-none disabled:opacity-40"
            style={{
              fontWeight: 500,
              background: 'var(--surface)',
              color: 'var(--text)',
              border: '1px solid var(--border)',
              transition: 'all 0.1s ease',
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.background = 'var(--hover)';
              e.currentTarget.style.borderColor = 'var(--text-muted)';
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = 'var(--surface)';
              e.currentTarget.style.borderColor = 'var(--border)';
            }}
          >
            Build CFG
          </button>
        </div>
      </motion.div>

      {/* Error banner */}
      <AnimatePresence>
        {error && (
          <motion.div
            className="mx-6 mb-2 px-3 py-2 rounded-lg text-xs"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
              color: 'var(--text-secondary)',
            }}
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            transition={{ duration: 0.12 }}
          >
            {error}
          </motion.div>
        )}
      </AnimatePresence>

      {/* Graph canvas */}
      <div className="flex-1 min-h-0 overflow-hidden px-6 pb-4 relative">
        {!connected || !pid ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2B1A'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Attach to a process to build CFG</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Select a process from the Processes panel</p>
          </motion.div>
        ) : !hasBuilt && !loading ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u22B6'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Enter a function address to build CFG</p>
            <p className="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>0x7FF600001000</p>
          </motion.div>
        ) : loading ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u22B6'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>Building control flow graph...</p>
          </motion.div>
        ) : graph && Object.keys(graph.nodes).length > 0 ? (
          <motion.div
            className="h-full rounded-lg overflow-hidden"
            style={{
              background: 'var(--surface)',
              border: '1px solid var(--border)',
            }}
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ duration: 0.15 }}
          >
            <svg
              ref={svgRef}
              className="w-full h-full"
              viewBox={`${viewBox.x} ${viewBox.y} ${viewBox.width} ${viewBox.height}`}
              onMouseDown={handleMouseDown}
              onMouseMove={handleMouseMove}
              onMouseUp={handleMouseUp}
              onMouseLeave={handleMouseUp}
              onWheel={handleWheel}
              style={{ cursor: dragging ? 'grabbing' : 'grab' }}
            >
              {/* Background */}
              <rect
                className="cfg-bg"
                x={viewBox.x}
                y={viewBox.y}
                width={viewBox.width}
                height={viewBox.height}
                fill="transparent"
              />

              {/* Arrow marker definitions */}
              <defs>
                <marker
                  id="arrow-default"
                  viewBox="0 0 10 6"
                  refX={10}
                  refY={3}
                  markerWidth={8}
                  markerHeight={5}
                  orient="auto-start-reverse"
                >
                  <path d="M 0 0 L 10 3 L 0 6 Z" fill="var(--text-muted)" />
                </marker>
                <marker
                  id="arrow-branch"
                  viewBox="0 0 10 6"
                  refX={10}
                  refY={3}
                  markerWidth={8}
                  markerHeight={5}
                  orient="auto-start-reverse"
                >
                  <path d="M 0 0 L 10 3 L 0 6 Z" fill="var(--syn-jump)" />
                </marker>
                <marker
                  id="arrow-call"
                  viewBox="0 0 10 6"
                  refX={10}
                  refY={3}
                  markerWidth={8}
                  markerHeight={5}
                  orient="auto-start-reverse"
                >
                  <path d="M 0 0 L 10 3 L 0 6 Z" fill="var(--syn-call)" />
                </marker>
                <marker
                  id="arrow-highlight"
                  viewBox="0 0 10 6"
                  refX={10}
                  refY={3}
                  markerWidth={8}
                  markerHeight={5}
                  orient="auto-start-reverse"
                >
                  <path d="M 0 0 L 10 3 L 0 6 Z" fill="var(--active-border)" />
                </marker>
              </defs>

              {/* Edges (behind nodes) */}
              {graph.edges.map((edge, i) => {
                const fromNode = graph.nodes[edge.from];
                const toNode = graph.nodes[edge.to];
                if (!fromNode || !toNode) return null;

                const highlighted = highlightedEdges.has(i);
                const path = buildEdgePath(fromNode, toNode, edge);
                const color = highlighted ? 'var(--active-border)' : getEdgeColor(edge);
                const dash = getEdgeDash(edge);
                const markerId = highlighted
                  ? 'arrow-highlight'
                  : edge.type === 'Branch' ? 'arrow-branch'
                  : edge.type === 'Call' ? 'arrow-call'
                  : 'arrow-default';

                return (
                  <path
                    key={i}
                    d={path}
                    fill="none"
                    stroke={color}
                    strokeWidth={highlighted ? 1.5 : 1}
                    strokeDasharray={dash}
                    markerEnd={`url(#${markerId})`}
                    opacity={highlightedEdges.size > 0 && !highlighted ? 0.25 : 1}
                    style={{ transition: 'opacity 0.15s ease, stroke-width 0.15s ease' }}
                  />
                );
              })}

              {/* Nodes */}
              {Object.entries(graph.nodes).map(([addr, node]) => (
                <NodeBlock
                  key={addr}
                  node={node}
                  isSelected={selectedNode === addr}
                  isHovered={hoveredNode === addr}
                  onSelect={() => setSelectedNode(selectedNode === addr ? null : addr)}
                  onHover={() => setHoveredNode(addr)}
                  onLeave={() => setHoveredNode(null)}
                />
              ))}
            </svg>

            {/* Tooltip */}
            <AnimatePresence>
              {tooltipNode && !dragging && (
                <motion.div
                  className="absolute top-4 right-4 px-3 py-2 rounded-lg text-xs font-mono"
                  style={{
                    background: 'var(--surface)',
                    border: '1px solid var(--border)',
                    color: 'var(--text)',
                    maxWidth: 280,
                    pointerEvents: 'none',
                  }}
                  initial={{ opacity: 0, y: -4 }}
                  animate={{ opacity: 1, y: 0 }}
                  exit={{ opacity: 0, y: -4 }}
                  transition={{ duration: 0.1 }}
                >
                  <div style={{ color: 'var(--text-secondary)' }}>
                    {tooltipNode.address}
                    <span style={{ color: 'var(--text-muted)' }}> {'\u2192'} {tooltipNode.end_address}</span>
                  </div>
                  <div className="mt-1" style={{ color: 'var(--text-muted)' }}>
                    {tooltipNode.instructions.length} instructions
                    {' \u00B7 '}{tooltipNode.size} bytes
                    {' \u00B7 '}{tooltipNode.type}
                  </div>
                  {tooltipNode.is_loop_header && (
                    <div className="mt-0.5" style={{ color: 'var(--syn-jump)' }}>
                      Loop header
                    </div>
                  )}
                </motion.div>
              )}
            </AnimatePresence>
          </motion.div>
        ) : hasBuilt && !error ? (
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u22B6'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No basic blocks found</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>The address may not point to a valid function</p>
          </motion.div>
        ) : null}
      </div>
    </div>
  );
}

export default CFGViewer;
