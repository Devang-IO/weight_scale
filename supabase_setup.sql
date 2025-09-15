-- Create weights table for plant measurements
CREATE TABLE IF NOT EXISTS weights (
    id SERIAL PRIMARY KEY,
    plant_0 DECIMAL(10,2) DEFAULT NULL,
    plant_1 DECIMAL(10,2) DEFAULT NULL,
    plant_2 DECIMAL(10,2) DEFAULT NULL,
    plant_3 DECIMAL(10,2) DEFAULT NULL,
    plant_4 DECIMAL(10,2) DEFAULT NULL,
    plant_5 DECIMAL(10,2) DEFAULT NULL,
    plant_6 DECIMAL(10,2) DEFAULT NULL,
    plant_7 DECIMAL(10,2) DEFAULT NULL,
    plant_8 DECIMAL(10,2) DEFAULT NULL,
    plant_9 DECIMAL(10,2) DEFAULT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Enable Row Level Security (RLS)
ALTER TABLE weights ENABLE ROW LEVEL SECURITY;

-- Create policy to allow all operations (adjust as needed for your security requirements)
CREATE POLICY "Allow all operations on weights" ON weights
    FOR ALL USING (true) WITH CHECK (true);

-- Create function to automatically update the updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ language 'plpgsql';

-- Create trigger to automatically update updated_at
CREATE TRIGGER update_weights_updated_at 
    BEFORE UPDATE ON weights 
    FOR EACH ROW 
    EXECUTE FUNCTION update_updated_at_column();

-- Insert dummy data for testing
INSERT INTO weights (plant_0, plant_1, plant_2, plant_3, plant_4, plant_5, plant_6, plant_7, plant_8, plant_9) VALUES
(125.50, 89.30, 156.75, 203.20, 78.90, 134.60, 167.40, 92.80, 145.30, 188.70),
(NULL, 91.20, 158.90, 201.50, NULL, 136.80, 165.20, 94.10, 147.60, 190.30),
(127.80, NULL, 154.30, 205.70, 80.40, NULL, 169.80, 91.50, 143.90, 186.20),
(124.20, 88.70, NULL, 199.80, 77.60, 132.90, NULL, 93.40, 149.70, 192.10),
(129.60, 90.50, 160.20, NULL, 82.30, 138.40, 171.60, NULL, 141.80, 184.50);

-- Insert some recent data (last few hours)
INSERT INTO weights (plant_1, plant_3, plant_7, created_at) VALUES
(87.40, 198.30, 95.70, NOW() - INTERVAL '2 hours'),
(89.80, 202.60, 97.20, NOW() - INTERVAL '1 hour'),
(91.10, 204.90, 98.50, NOW() - INTERVAL '30 minutes');

-- Insert very recent data (last 10 minutes)
INSERT INTO weights (plant_0, plant_2, plant_5, plant_8, created_at) VALUES
(131.20, 162.80, 140.70, 152.40, NOW() - INTERVAL '10 minutes'),
(128.90, 159.60, 137.30, 148.90, NOW() - INTERVAL '5 minutes');