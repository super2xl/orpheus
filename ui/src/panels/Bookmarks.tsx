import { useState, useEffect, useCallback, useMemo } from 'react';
import { motion, AnimatePresence } from 'motion/react';
import { useBookmarks } from '../hooks/useBookmarks';
import { useConnection } from '../hooks/useConnection';
import { useContextMenu } from '../hooks/useContextMenu';
import ContextMenu from '../components/ContextMenu';

function formatRelativeTime(timestamp: number): string {
  const now = Date.now() / 1000;
  const diff = now - timestamp;

  if (diff < 60) return 'just now';
  if (diff < 3600) {
    const mins = Math.floor(diff / 60);
    return `${mins} minute${mins !== 1 ? 's' : ''} ago`;
  }
  if (diff < 86400) {
    const hours = Math.floor(diff / 3600);
    return `${hours} hour${hours !== 1 ? 's' : ''} ago`;
  }
  const days = Math.floor(diff / 86400);
  return `${days} day${days !== 1 ? 's' : ''} ago`;
}

function Bookmarks({ onNavigate }: { onNavigate?: (panel: string, address?: string) => void }) {
  const { connected } = useConnection();
  const { bookmarks, loading, error, refresh, add, remove, update } = useBookmarks();

  const [activeCategory, setActiveCategory] = useState('All');
  const [showAddForm, setShowAddForm] = useState(false);
  const [editingIndex, setEditingIndex] = useState<number | null>(null);

  // Add form state
  const [newAddress, setNewAddress] = useState('');
  const [newLabel, setNewLabel] = useState('');
  const [newNotes, setNewNotes] = useState('');
  const [newCategory, setNewCategory] = useState('');
  const [newModule, setNewModule] = useState('');

  // Edit form state
  const [editLabel, setEditLabel] = useState('');
  const [editNotes, setEditNotes] = useState('');
  const [editCategory, setEditCategory] = useState('');

  // Category autocomplete
  const [showCategorySuggestions, setShowCategorySuggestions] = useState(false);
  const { menu, show: showContextMenu, close: closeContextMenu } = useContextMenu();

  // Fetch bookmarks when connected
  useEffect(() => {
    if (connected) {
      refresh();
    }
  }, [connected, refresh]);

  // Extract unique categories
  const categories = useMemo(() => {
    const cats = new Set<string>();
    bookmarks.forEach((b) => {
      if (b.category) cats.add(b.category);
    });
    return Array.from(cats).sort();
  }, [bookmarks]);

  // Filter by category
  const filtered = useMemo(() => {
    if (activeCategory === 'All') return bookmarks;
    return bookmarks.filter((b) => b.category === activeCategory);
  }, [bookmarks, activeCategory]);

  // Category suggestions for autocomplete
  const categorySuggestions = useMemo(() => {
    if (!newCategory.trim()) return categories;
    const q = newCategory.toLowerCase();
    return categories.filter((c) => c.toLowerCase().includes(q));
  }, [categories, newCategory]);

  const handleAdd = useCallback(async () => {
    if (!newAddress.trim() || !newLabel.trim()) return;
    await add({
      address: newAddress.trim(),
      label: newLabel.trim(),
      notes: newNotes.trim(),
      category: newCategory.trim(),
      module: newModule.trim(),
    });
    setNewAddress('');
    setNewLabel('');
    setNewNotes('');
    setNewCategory('');
    setNewModule('');
    setShowAddForm(false);
  }, [newAddress, newLabel, newNotes, newCategory, newModule, add]);

  const handleCancelAdd = useCallback(() => {
    setNewAddress('');
    setNewLabel('');
    setNewNotes('');
    setNewCategory('');
    setNewModule('');
    setShowAddForm(false);
  }, []);

  const handleStartEdit = useCallback((index: number) => {
    const bm = bookmarks[index];
    setEditingIndex(index);
    setEditLabel(bm.label);
    setEditNotes(bm.notes);
    setEditCategory(bm.category);
  }, [bookmarks]);

  const handleSaveEdit = useCallback(async () => {
    if (editingIndex === null) return;
    await update(editingIndex, {
      label: editLabel.trim(),
      notes: editNotes.trim(),
      category: editCategory.trim(),
    });
    setEditingIndex(null);
  }, [editingIndex, editLabel, editNotes, editCategory, update]);

  const handleCancelEdit = useCallback(() => {
    setEditingIndex(null);
  }, []);

  const handleDelete = useCallback(async (index: number) => {
    await remove(index);
  }, [remove]);

  const inputStyle = {
    background: 'var(--surface)',
    border: '1px solid var(--border)',
    color: 'var(--text)',
    transition: 'border-color 0.1s ease',
  };

  const handleInputFocus = (e: React.FocusEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    e.currentTarget.style.borderColor = 'var(--text-muted)';
  };

  const handleInputBlur = (e: React.FocusEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    e.currentTarget.style.borderColor = 'var(--border)';
  };

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
              Bookmarks
            </h1>
            {bookmarks.length > 0 && (
              <span
                className="text-xs px-2 py-0.5 rounded-md font-mono"
                style={{
                  color: 'var(--text-secondary)',
                  background: 'var(--active)',
                }}
              >
                {filtered.length}
                {activeCategory !== 'All' && ` / ${bookmarks.length}`}
              </span>
            )}
          </div>
          <button
            onClick={() => setShowAddForm(!showAddForm)}
            className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
            style={{
              fontWeight: 400,
              background: 'transparent',
              color: 'var(--text-secondary)',
              border: '1px solid var(--border)',
              transition: 'all 0.1s ease',
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.background = 'var(--hover)';
              e.currentTarget.style.color = 'var(--text)';
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = 'transparent';
              e.currentTarget.style.color = 'var(--text-secondary)';
            }}
          >
            {showAddForm ? 'Cancel' : '+ Add'}
          </button>
        </div>

        {/* Category tabs */}
        {categories.length > 0 && (
          <div className="flex items-center gap-1.5 flex-wrap">
            {['All', ...categories].map((cat) => {
              const isActive = activeCategory === cat;
              return (
                <button
                  key={cat}
                  onClick={() => setActiveCategory(cat)}
                  className="px-2.5 h-6 rounded-full text-[11px] cursor-pointer border-none outline-none"
                  style={{
                    fontWeight: isActive ? 500 : 400,
                    background: isActive ? 'var(--active)' : 'transparent',
                    color: isActive ? 'var(--text)' : 'var(--text-muted)',
                    border: '1px solid ' + (isActive ? 'var(--border)' : 'transparent'),
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.background = 'var(--hover)';
                      e.currentTarget.style.color = 'var(--text-secondary)';
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isActive) {
                      e.currentTarget.style.background = 'transparent';
                      e.currentTarget.style.color = 'var(--text-muted)';
                    }
                  }}
                >
                  {cat}
                </button>
              );
            })}
          </div>
        )}

        {/* Add bookmark form */}
        <AnimatePresence>
          {showAddForm && (
            <motion.div
              className="space-y-2.5 rounded-lg p-4"
              style={{
                background: 'var(--surface)',
                border: '1px solid var(--border)',
              }}
              initial={{ opacity: 0, height: 0 }}
              animate={{ opacity: 1, height: 'auto' }}
              exit={{ opacity: 0, height: 0 }}
              transition={{ duration: 0.15 }}
            >
              <div className="flex gap-2.5">
                <input
                  type="text"
                  placeholder="Address (e.g. 0x7FF...)"
                  value={newAddress}
                  onChange={(e) => setNewAddress(e.target.value)}
                  onFocus={handleInputFocus}
                  onBlur={handleInputBlur}
                  className="flex-1 h-8 px-2.5 rounded-md text-xs font-mono outline-none"
                  style={inputStyle}
                />
                <input
                  type="text"
                  placeholder="Label"
                  value={newLabel}
                  onChange={(e) => setNewLabel(e.target.value)}
                  onFocus={handleInputFocus}
                  onBlur={handleInputBlur}
                  className="flex-1 h-8 px-2.5 rounded-md text-xs outline-none"
                  style={inputStyle}
                />
              </div>
              <textarea
                placeholder="Notes (optional)"
                value={newNotes}
                onChange={(e) => setNewNotes(e.target.value)}
                onFocus={handleInputFocus as any}
                onBlur={handleInputBlur as any}
                className="w-full h-16 px-2.5 py-2 rounded-md text-xs outline-none resize-none"
                style={inputStyle}
              />
              <div className="flex gap-2.5">
                <div className="flex-1 relative">
                  <input
                    type="text"
                    placeholder="Category"
                    value={newCategory}
                    onChange={(e) => setNewCategory(e.target.value)}
                    onFocus={(e) => {
                      handleInputFocus(e);
                      setShowCategorySuggestions(true);
                    }}
                    onBlur={(e) => {
                      handleInputBlur(e);
                      // Delay hiding to allow click
                      setTimeout(() => setShowCategorySuggestions(false), 150);
                    }}
                    className="w-full h-8 px-2.5 rounded-md text-xs outline-none"
                    style={inputStyle}
                  />
                  <AnimatePresence>
                    {showCategorySuggestions && categorySuggestions.length > 0 && (
                      <motion.div
                        className="absolute top-full left-0 right-0 mt-1 rounded-md overflow-hidden z-20"
                        style={{
                          background: 'var(--surface)',
                          border: '1px solid var(--border)',
                        }}
                        initial={{ opacity: 0, y: -4 }}
                        animate={{ opacity: 1, y: 0 }}
                        exit={{ opacity: 0, y: -4 }}
                        transition={{ duration: 0.1 }}
                      >
                        {categorySuggestions.map((cat) => (
                          <button
                            key={cat}
                            onMouseDown={() => {
                              setNewCategory(cat);
                              setShowCategorySuggestions(false);
                            }}
                            className="w-full text-left px-2.5 py-1.5 text-xs cursor-pointer border-none outline-none"
                            style={{
                              background: 'transparent',
                              color: 'var(--text-secondary)',
                              transition: 'all 0.1s ease',
                            }}
                            onMouseEnter={(e) => {
                              e.currentTarget.style.background = 'var(--hover)';
                              e.currentTarget.style.color = 'var(--text)';
                            }}
                            onMouseLeave={(e) => {
                              e.currentTarget.style.background = 'transparent';
                              e.currentTarget.style.color = 'var(--text-secondary)';
                            }}
                          >
                            {cat}
                          </button>
                        ))}
                      </motion.div>
                    )}
                  </AnimatePresence>
                </div>
                <input
                  type="text"
                  placeholder="Module (auto-detected)"
                  value={newModule}
                  onChange={(e) => setNewModule(e.target.value)}
                  onFocus={handleInputFocus}
                  onBlur={handleInputBlur}
                  className="flex-1 h-8 px-2.5 rounded-md text-xs font-mono outline-none"
                  style={inputStyle}
                />
              </div>
              <div className="flex justify-end gap-2">
                <button
                  onClick={handleCancelAdd}
                  className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-muted)',
                    transition: 'color 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.color = 'var(--text-muted)';
                  }}
                >
                  Cancel
                </button>
                <button
                  onClick={handleAdd}
                  disabled={!newAddress.trim() || !newLabel.trim()}
                  className="px-3 h-7 rounded-md text-xs cursor-pointer border-none outline-none disabled:opacity-40"
                  style={{
                    fontWeight: 400,
                    background: 'transparent',
                    color: 'var(--text-secondary)',
                    border: '1px solid var(--border)',
                    transition: 'all 0.1s ease',
                  }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = 'var(--hover)';
                    e.currentTarget.style.color = 'var(--text)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent';
                    e.currentTarget.style.color = 'var(--text-secondary)';
                  }}
                >
                  Save
                </button>
              </div>
            </motion.div>
          )}
        </AnimatePresence>
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

      {/* Bookmark list */}
      <div className="flex-1 min-h-0 overflow-auto px-6 pb-4">
        {loading && bookmarks.length === 0 ? (
          /* Loading skeleton */
          <div className="space-y-2 pt-2">
            {Array.from({ length: 4 }, (_, i) => (
              <div
                key={i}
                className="rounded-lg p-4"
                style={{ background: 'var(--surface)', border: '1px solid var(--border)' }}
              >
                <motion.div
                  className="h-3.5 rounded w-1/3 mb-2"
                  style={{ background: 'var(--skeleton)' }}
                  animate={{ opacity: [0.3, 0.5, 0.3] }}
                  transition={{ duration: 2, repeat: Infinity, ease: 'easeInOut', delay: i * 0.1 }}
                />
                <motion.div
                  className="h-3 rounded w-2/3"
                  style={{ background: 'var(--skeleton)' }}
                  animate={{ opacity: [0.3, 0.5, 0.3] }}
                  transition={{ duration: 2, repeat: Infinity, ease: 'easeInOut', delay: i * 0.1 + 0.05 }}
                />
              </div>
            ))}
          </div>
        ) : bookmarks.length === 0 ? (
          /* Empty state: no bookmarks */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2606'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No bookmarks yet</p>
            <p className="text-xs" style={{ color: 'var(--text-muted)' }}>Save addresses for quick reference</p>
          </motion.div>
        ) : filtered.length === 0 ? (
          /* Empty state: no matches for category */
          <motion.div
            className="h-full flex flex-col items-center justify-center gap-3"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ delay: 0.1 }}
          >
            <div className="text-3xl" style={{ color: 'var(--text-muted)' }}>{'\u2606'}</div>
            <p className="text-sm" style={{ color: 'var(--text-secondary)' }}>No bookmarks in this category</p>
          </motion.div>
        ) : (
          /* Bookmark cards */
          <div className="space-y-2 pt-1">
            {filtered.map((bm, index) => {
              const actualIndex = bookmarks.indexOf(bm);
              const isEditing = editingIndex === actualIndex;

              return (
                <motion.div
                  key={`${bm.address}-${actualIndex}`}
                  className="rounded-lg p-4 group"
                  style={{
                    background: 'var(--surface)',
                    border: '1px solid var(--border)',
                    transition: 'border-color 0.1s ease',
                  }}
                  onContextMenu={(e) => showContextMenu(e, [
                    { label: 'View in Memory', action: () => onNavigate?.('memory', bm.address) },
                    { label: 'View in Disassembly', action: () => onNavigate?.('disassembly', bm.address) },
                    { label: 'Edit', action: () => handleStartEdit(actualIndex), separator: true },
                    { label: 'Delete', action: () => handleDelete(actualIndex) },
                  ])}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.borderColor = 'var(--text-muted)';
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.borderColor = 'var(--border)';
                  }}
                  initial={{ opacity: 0, x: -4 }}
                  animate={{ opacity: 1, x: 0 }}
                  transition={{
                    duration: 0.1,
                    ease: 'easeOut',
                    delay: Math.min(index * 0.02, 0.2),
                  }}
                  layout
                >
                  {isEditing ? (
                    /* Edit mode */
                    <div className="space-y-2">
                      <div className="font-mono text-xs" style={{ color: 'var(--text-secondary)' }}>
                        {bm.address}
                      </div>
                      <input
                        type="text"
                        value={editLabel}
                        onChange={(e) => setEditLabel(e.target.value)}
                        onFocus={handleInputFocus}
                        onBlur={handleInputBlur}
                        className="w-full h-7 px-2 rounded-md text-xs outline-none"
                        style={inputStyle}
                        placeholder="Label"
                        autoFocus
                      />
                      <textarea
                        value={editNotes}
                        onChange={(e) => setEditNotes(e.target.value)}
                        onFocus={handleInputFocus as any}
                        onBlur={handleInputBlur as any}
                        className="w-full h-14 px-2 py-1.5 rounded-md text-xs outline-none resize-none"
                        style={inputStyle}
                        placeholder="Notes"
                      />
                      <input
                        type="text"
                        value={editCategory}
                        onChange={(e) => setEditCategory(e.target.value)}
                        onFocus={handleInputFocus}
                        onBlur={handleInputBlur}
                        className="w-full h-7 px-2 rounded-md text-xs outline-none"
                        style={inputStyle}
                        placeholder="Category"
                      />
                      <div className="flex justify-end gap-2 pt-1">
                        <button
                          onClick={handleCancelEdit}
                          className="px-2.5 h-6 rounded-md text-[11px] cursor-pointer border-none outline-none"
                          style={{
                            fontWeight: 400,
                            background: 'transparent',
                            color: 'var(--text-muted)',
                            transition: 'color 0.1s ease',
                          }}
                          onMouseEnter={(e) => {
                            e.currentTarget.style.color = 'var(--text)';
                          }}
                          onMouseLeave={(e) => {
                            e.currentTarget.style.color = 'var(--text-muted)';
                          }}
                        >
                          Cancel
                        </button>
                        <button
                          onClick={handleSaveEdit}
                          className="px-2.5 h-6 rounded-md text-[11px] cursor-pointer border-none outline-none"
                          style={{
                            fontWeight: 400,
                            background: 'transparent',
                            color: 'var(--text-secondary)',
                            border: '1px solid var(--border)',
                            transition: 'all 0.1s ease',
                          }}
                          onMouseEnter={(e) => {
                            e.currentTarget.style.background = 'var(--hover)';
                            e.currentTarget.style.color = 'var(--text)';
                          }}
                          onMouseLeave={(e) => {
                            e.currentTarget.style.background = 'transparent';
                            e.currentTarget.style.color = 'var(--text-secondary)';
                          }}
                        >
                          Save
                        </button>
                      </div>
                    </div>
                  ) : (
                    /* View mode */
                    <>
                      <div className="flex items-start justify-between gap-3">
                        <div className="min-w-0 flex-1">
                          {/* Label */}
                          <div className="text-sm" style={{ color: 'var(--text)', fontWeight: 500 }}>
                            {bm.label}
                          </div>
                          {/* Address */}
                          <div
                            className="font-mono text-xs mt-0.5 cursor-pointer"
                            style={{ color: 'var(--text-secondary)', transition: 'color 0.1s ease' }}
                            onMouseEnter={(e) => {
                              e.currentTarget.style.color = 'var(--text)';
                            }}
                            onMouseLeave={(e) => {
                              e.currentTarget.style.color = 'var(--text-secondary)';
                            }}
                          >
                            {bm.address}
                          </div>
                        </div>
                        {/* Actions (visible on hover) */}
                        <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100" style={{ transition: 'opacity 0.1s ease' }}>
                          <button
                            onClick={() => handleStartEdit(actualIndex)}
                            className="px-2 py-0.5 rounded text-[10px] cursor-pointer border-none outline-none"
                            style={{
                              fontWeight: 400,
                              background: 'transparent',
                              color: 'var(--text-muted)',
                              transition: 'color 0.1s ease',
                            }}
                            onMouseEnter={(e) => {
                              e.currentTarget.style.color = 'var(--text)';
                            }}
                            onMouseLeave={(e) => {
                              e.currentTarget.style.color = 'var(--text-muted)';
                            }}
                          >
                            Edit
                          </button>
                          <button
                            onClick={() => handleDelete(actualIndex)}
                            className="px-2 py-0.5 rounded text-[10px] cursor-pointer border-none outline-none"
                            style={{
                              fontWeight: 400,
                              background: 'transparent',
                              color: 'var(--text-muted)',
                              transition: 'color 0.1s ease',
                            }}
                            onMouseEnter={(e) => {
                              e.currentTarget.style.color = 'var(--text)';
                            }}
                            onMouseLeave={(e) => {
                              e.currentTarget.style.color = 'var(--text-muted)';
                            }}
                          >
                            Delete
                          </button>
                        </div>
                      </div>
                      {/* Notes */}
                      {bm.notes && (
                        <p className="text-xs mt-2 leading-relaxed" style={{ color: 'var(--text-secondary)' }}>
                          {bm.notes}
                        </p>
                      )}
                      {/* Meta row */}
                      <div className="flex items-center gap-2 mt-2.5 flex-wrap">
                        {bm.category && (
                          <span
                            className="text-[10px] px-1.5 py-0.5 rounded-full"
                            style={{
                              fontWeight: 400,
                              background: 'var(--active)',
                              color: 'var(--text-secondary)',
                              border: '1px solid var(--border)',
                            }}
                          >
                            {bm.category}
                          </span>
                        )}
                        {bm.module && (
                          <span className="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                            {bm.module}
                          </span>
                        )}
                        <span className="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                          {formatRelativeTime(bm.created_at)}
                        </span>
                      </div>
                    </>
                  )}
                </motion.div>
              );
            })}
          </div>
        )}
      </div>

      {/* Context menu */}
      <AnimatePresence>
        {menu && (
          <ContextMenu
            x={menu.x}
            y={menu.y}
            items={menu.items}
            onClose={closeContextMenu}
          />
        )}
      </AnimatePresence>
    </div>
  );
}

export default Bookmarks;
