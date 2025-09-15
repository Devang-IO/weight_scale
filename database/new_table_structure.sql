-- New optimized table structure for raw weight data
-- This replaces the old single-weight approach with real-time data streaming

-- Drop old table if exists
DROP TABLE IF EXISTS weights;

-- Create new raw readings table
CREATE TABLE weight_readings (
  id SERIAL PRIMARY KEY,
  plant_number INTEGER NOT NULL,
  weight INTEGER NOT NULL, -- Weight in grams (whole numbers only)
  session_id TEXT NOT NULL, -- Groups readings from same weighing session
  timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
  
  -- Indexes for fast queries
  INDEX idx_plant_number (plant_number),
  INDEX idx_session_id (session_id),
  INDEX idx_timestamp (timestamp)
);

-- Enable Row Level Security (RLS) if using Supabase
ALTER TABLE weight_readings ENABLE ROW LEVEL SECURITY;

-- Create policy for public access (adjust as needed for your security requirements)
CREATE POLICY "Allow all operations on weight_readings" ON weight_readings
  FOR ALL USING (true);

-- Optional: Create a view for easy session summaries
CREATE VIEW session_summaries AS
SELECT 
  session_id,
  plant_number,
  COUNT(*) as reading_count,
  AVG(weight) as avg_weight,
  MIN(weight) as min_weight,
  MAX(weight) as max_weight,
  STDDEV(weight) as weight_stddev,
  MIN(timestamp) as session_start,
  MAX(timestamp) as session_end,
  EXTRACT(EPOCH FROM (MAX(timestamp) - MIN(timestamp))) as duration_seconds
FROM weight_readings
GROUP BY session_id, plant_number
ORDER BY session_start DESC;

-- Optional: Create indexes on the view for better performance
CREATE INDEX idx_session_summaries_plant ON weight_readings (plant_number, session_id);
CREATE INDEX idx_session_summaries_time ON weight_readings (timestamp DESC);